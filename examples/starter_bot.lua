for line in io.lines() do
    local id = line:match('"request_id"%s*:%s*"(%d+)"')
    if id then
        io.write('{"type":"PLAY","request_id":"', id, '","commands":["HARD_DROP"]}\n')
        io.flush()
    end
end
