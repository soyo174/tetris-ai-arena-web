import std.regex : matchFirst, regex;
import std.stdio : stdin, stdout, writefln;

void main() {
    auto requestId = regex("\"request_id\"\\s*:\\s*\"([0-9]+)\"");
    foreach (line; stdin.byLine) {
        auto request = matchFirst(line, requestId);
        if (request.empty) continue;
        writefln("{\"type\":\"PLAY\",\"request_id\":\"%s\",\"commands\":[\"HARD_DROP\"]}", request[1]);
        stdout.flush();
    }
}
