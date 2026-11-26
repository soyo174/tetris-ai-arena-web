#!/usr/bin/env bash

request_id='"request_id"[[:space:]]*:[[:space:]]*"([0-9]+)"'
while IFS= read -r line; do
    if [[ $line =~ $request_id ]]; then
        printf '{"type":"PLAY","request_id":"%s","commands":["HARD_DROP"]}\n' "${BASH_REMATCH[1]}"
    fi
done
