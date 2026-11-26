const readline = require("node:readline");

const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
input.on("line", line => {
  const request = JSON.parse(line);
  process.stdout.write(JSON.stringify({
    type: "PLAY",
    request_id: request.request_id,
    commands: ["HARD_DROP"],
  }) + "\n");
});
