module Bot

open System
open System.Text.Json

Seq.initInfinite (fun _ -> Console.ReadLine())
|> Seq.takeWhile (not << isNull)
|> Seq.iter (fun line ->
    use request = JsonDocument.Parse(line)
    let response =
        {| ``type`` = "PLAY"
           request_id = request.RootElement.GetProperty("request_id").GetString()
           commands = [| "HARD_DROP" |] |}
    Console.WriteLine(JsonSerializer.Serialize(response))
    Console.Out.Flush())
