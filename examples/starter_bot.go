package main

import (
	"bufio"
	"encoding/json"
	"os"
)

func main() {
	input := bufio.NewScanner(os.Stdin)
	output := json.NewEncoder(os.Stdout)
	for input.Scan() {
		var request struct {
			RequestID string `json:"request_id"`
		}
		if err := json.Unmarshal(input.Bytes(), &request); err != nil {
			continue
		}
		err := output.Encode(map[string]any{
			"type":       "PLAY",
			"request_id": request.RequestID,
			"commands":   []string{"HARD_DROP"},
		})
		if err != nil {
			return
		}
	}
}
