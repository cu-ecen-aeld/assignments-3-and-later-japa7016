#!/bin/sh

COMMAND=$1
DAEMON_NAME="aesdsocket"
DAEMON_PATH="/usr/bin/aesdsocket"

if [ -z "$COMMAND" ]; then
    echo "Usage: $0 start|stop"
    exit 1
fi

case "$COMMAND" in
    start)
        echo "Launching ${DAEMON_NAME}..."
        if start-stop-daemon -S -n ${DAEMON_NAME} -a ${DAEMON_PATH} -- -d; then
            echo "${DAEMON_NAME} started successfully."
        else
            echo "Failed to launch ${DAEMON_NAME}."
            exit 1
        fi
        ;;

    stop)
        echo "Shutting down ${DAEMON_NAME}..."
        if start-stop-daemon -K -n ${DAEMON_NAME} --signal SIGTERM; then
            echo "${DAEMON_NAME} stopped."
        else
            echo "Failed to stop ${DAEMON_NAME}."
            exit 1
        fi
        ;;

    *)
        echo "Invalid command. Usage: $0 start|stop"
        exit 1
        ;;
esac

exit 0

