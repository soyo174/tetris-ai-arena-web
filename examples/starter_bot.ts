import { createInterface } from "node:readline";

type ObservationRequest = { request_id: string };

const input = createInterface({ input: process.stdin, crlfDelay: Infinity });
input.on("line", line => {
  const request: ObservationRequest = JSON.parse(line);
  process.stdout.write(JSON.stringify({
    type: "PLAY",
    request_id: request.request_id,
    commands: ["HARD_DROP"],
  }) + "\n");
});
