FROM alpine:3.15

RUN mkdir /opt/app
WORKDIR /opt/app

RUN apk update && apk add nodejs

COPY ./service1.js /opt/app/service1.js
CMD ["node", "service1.js"]
