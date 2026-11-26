<?php

while (($line = fgets(STDIN)) !== false) {
    $request = json_decode($line, true, 512, JSON_THROW_ON_ERROR);
    fwrite(STDOUT, json_encode([
        'type' => 'PLAY',
        'request_id' => $request['request_id'],
        'commands' => ['HARD_DROP'],
    ], JSON_THROW_ON_ERROR) . "\n");
    fflush(STDOUT);
}
