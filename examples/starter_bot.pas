program StarterBot;
{$mode objfpc}{$H+}

function RequestID(const Line: string): string;
const
    Key = '"request_id"';
var
    Cursor, Start: SizeInt;
begin
    Result := '';
    Cursor := Pos(Key, Line);
    if Cursor = 0 then Exit;
    Inc(Cursor, Length(Key));
    while (Cursor <= Length(Line)) and (Line[Cursor] in [' ', #9]) do Inc(Cursor);
    if (Cursor > Length(Line)) or (Line[Cursor] <> ':') then Exit;
    Inc(Cursor);
    while (Cursor <= Length(Line)) and (Line[Cursor] in [' ', #9]) do Inc(Cursor);
    if (Cursor > Length(Line)) or (Line[Cursor] <> '"') then Exit;
    Inc(Cursor);
    Start := Cursor;
    while (Cursor <= Length(Line)) and (Line[Cursor] in ['0'..'9']) do Inc(Cursor);
    if (Cursor > Length(Line)) or (Line[Cursor] <> '"') then Exit;
    Result := Copy(Line, Start, Cursor - Start);
end;

var
    Line, ID: string;
begin
    while not Eof(Input) do
    begin
        ReadLn(Line);
        ID := RequestID(Line);
        if ID = '' then Continue;
        WriteLn('{"type":"PLAY","request_id":"', ID, '","commands":["HARD_DROP"]}');
        Flush(Output);
    end;
end.
