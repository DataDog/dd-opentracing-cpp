FROM alpine:3.15

RUN mkdir /opt/app
WORKDIR /opt/app

# Note: WORKDIR must already be set (as it is above) before installing npm.
# If WORKDIR is not set, then npm is installed at the container root,
# which then causes `npm install` to fail later.
RUN apk update && apk add nodejs npm
RUN npm install massagepack

COPY ./agent.js /opt/app/agent.js

CMD ["node", "agent.js"]
