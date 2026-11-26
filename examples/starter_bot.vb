Imports System
Imports System.Text.Json

Module Program
    Sub Main()
        While True
            Dim line As String = Console.ReadLine()
            If line Is Nothing Then Exit While
            Using request = JsonDocument.Parse(line)
                Dim response = New With {
                    .[type] = "PLAY",
                    .request_id = request.RootElement.GetProperty("request_id").GetString(),
                    .commands = New String() {"HARD_DROP"}
                }
                Console.WriteLine(JsonSerializer.Serialize(response))
                Console.Out.Flush()
            End Using
        End While
    End Sub
End Module
