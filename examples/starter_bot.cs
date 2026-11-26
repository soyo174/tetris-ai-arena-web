using System;
using System.Text.Json;

for (string? line; (line = Console.ReadLine()) != null;)
{
    using var request = JsonDocument.Parse(line);
    var response = new
    {
        type = "PLAY",
        request_id = request.RootElement.GetProperty("request_id").GetString(),
        commands = new[] { "HARD_DROP" }
    };
    Console.WriteLine(JsonSerializer.Serialize(response));
    Console.Out.Flush();
}
