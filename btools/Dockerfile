# check=skip=InvalidDefaultArgInFrom   $PLATFORM unset produces good error
ARG PLATFORM
FROM ${PLATFORM}
ARG FRONTEND PLATFORM
COPY common.bash prereq-inst /tmp/
RUN /tmp/prereq-inst ${FRONTEND} && rm /tmp/common.bash /tmp/prereq-inst
