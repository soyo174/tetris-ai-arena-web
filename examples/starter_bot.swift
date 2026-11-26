import Foundation

struct Request: Decodable {
    let request_id: String
}

struct Response: Encodable {
    let type = "PLAY"
    let request_id: String
    let commands = ["HARD_DROP"]
}

let decoder = JSONDecoder()
let encoder = JSONEncoder()
while let line = readLine() {
    let request = try decoder.decode(Request.self, from: Data(line.utf8))
    var reply = try encoder.encode(Response(request_id: request.request_id))
    reply.append(0x0a)
    FileHandle.standardOutput.write(reply)
}
