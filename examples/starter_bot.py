"""TETRA ARENA 单文件示例。协议使用标准输入与标准输出，每行一个 JSON。"""
import json
import sys


def decide(observation):
    """示例在最矮列附近落子。board[0] 是棋盘底行，0 表示空格。"""
    board = observation["self"]["board"]
    heights = [
        max((y + 1 for y, row in enumerate(board) if row[x]), default=0)
        for x in range(10)
    ]
    target = min(range(10), key=lambda x: (heights[x], abs(x - 4)))
    move = "LEFT" if target < 4 else "RIGHT"
    return [move] * abs(target - 4) + ["HARD_DROP"]


for line in sys.stdin:
    request = json.loads(line)
    print(json.dumps({
        "type": "PLAY",
        "request_id": request["request_id"],
        "commands": decide(request["observation"]),
    }), flush=True)
