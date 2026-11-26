require "json"

$stdout.sync = true
$stdin.each_line do |line|
  request = JSON.parse(line)
  puts JSON.generate(
    type: "PLAY",
    request_id: request.fetch("request_id"),
    commands: ["HARD_DROP"],
  )
end
