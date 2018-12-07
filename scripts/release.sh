#!/bin/bash
set -e

IS_PRERELEASE=true

if [[ ! $- == *i* ]]
then
    echo "Warning: this is an interactive script"
fi

if [[ -z $CIRCLE_CI_TOKEN ]]
then
  echo "Please provide a CircleCI API token in \$CIRCLE_CI_TOKEN"
  exit 1
fi

if [[ -z $GITHUB_TOKEN ]]
then
  echo "Please provide a Github personal access token in \$GITHUB_TOKEN"
  exit 1
fi

if [[ -z $GOPATH ]]
then
  echo "Please install Golang"
  exit 1
fi

if ! [[ -f "$GOPATH/bin/hub" ]]
then
  echo "Installing required tool Github 'hub'"
  go get github.com/github/hub
fi

echo "Note: Make sure that you can sign commits on this machine with your GPG key: "
echo "https://help.github.com/articles/signing-commits/"
echo ""

read -p "Creating a release off \"$(git symbolic-ref --short HEAD)\", is this correct? [Y/n] "  Y
if ! [[ -z $Y || $Y == "y" || $Y == "Y" ]]
then
  exit 0
fi

read -p "Enter release version (eg v1.2.3 or test-myfeature) " VERSION
if [[ -z $VERSION ]]
then
  echo "Please enter a version"
  exit 1
elif [[ -n $(git ls-remote --tags origin $VERSION) ]]
then
  Y=
  read -p "The tag $VERSION already exists, continue with release using this tag? [Y/n] " Y
  if ! [[ -z $Y || $Y == "y" || $Y == "Y" ]]
  then
    exit 0
  fi
else
  read -p "Press enter to edit git tag description " Y
  git tag -s $VERSION
  git push origin $VERSION
fi

echo "Waiting on CircleCI build..."

# Start a build job on CircleCI
BUILD_NUM=$(curl -s -X POST --header "Content-Type: application/json" -d '{
  "tag": "'$VERSION'",
  "build_parameters": {
    "BUILD_ALL_NGINX_VERSIONS": "1"
  }
}
' https://circleci.com/api/v1.1/project/github/DataDog/dd-opentracing-cpp?circle-token=${CIRCLE_CI_TOKEN} | jq '.build_num') 

# Wait for CircleCI build job to finish
while : ; do
  BUILD_RESULT=$(curl -s https://circleci.com/api/v1.1/project/github/DataDog/dd-opentracing-cpp/${BUILD_NUM}?circle-token=${CIRCLE_CI_TOKEN})
  STATUS=$(echo $BUILD_RESULT | jq -r '.status')

  case $STATUS in
    canceled|infrastructure_fail|timedout|not_run|failed|no_tests) echo "Failed with status: ${STATUS}" && exit 1 ;;
    fixed|success) break ;;
    retried|running|queued|scheduled|not_running) ;; # Sleep and try again.
    *) echo "Unknown status: ${STATUS}" && exit 1 ;;
  esac

  echo "Waiting on CI, current status: ${STATUS}"
  sleep 30
done

# Download artifacts
echo "Build status \"$STATUS\", downloading artifacts..."
ARTIFACT_URLS=$(curl -s https://circleci.com/api/v1.1/project/github/DataDog/dd-opentracing-cpp/${BUILD_NUM}/artifacts?circle-token=${CIRCLE_CI_TOKEN} | jq -r '.[] | .url')

rm -rf .bin
mkdir .bin
cd .bin
echo "${ARTIFACT_URLS}" | while read ARTIFACT_URL
  do
    echo "Downloading artifact: ${ARTIFACT_URL}"
    curl -s -O ${ARTIFACT_URL}
  done

# Process and sign artifacts
gzip libdd_opentracing_plugin.so
mv libdd_opentracing_plugin.so.gz linux-amd64-libdd_opentracing_plugin.so.gz

for ARTIFACT in ./*; do
  gpg --armor --detach-sign "${ARTIFACT}"
done

assets=()
for f in ./*; do
  [ -f "$f" ] && assets+=(-a "$f")
done

# Create a github release
PRERELEASE=$([ $IS_PRERELEASE = true ] && echo "-p" || echo "")
$GOPATH/bin/hub release create $PRERELEASE \
  "${assets[@]}" \
  -m "Release $VERSION" $VERSION
cd ..
rm -rf .bin

echo "Successfully created release $VERSION, visit the URL above to edit the release description"
