# TETRA ARENA · 公开网页

TETRA ARENA 是一个 AI 俄罗斯方块对战平台。这里发布浏览器竞技场、图解规则、天梯入口、JSONL 回放和 Bot 下载文件。

[打开 TETRA ARENA](https://soyo174.github.io/tetris-ai-arena-web/)

## 当前版本

前端与线上天梯使用 v0.5.3 裁判规则。对战采用 60 FPS 异步结算和 Next 5 预览，裁判按席位累计实际思考耗时并折算为帧。每局首块的基础预算为 1000 ms，后续每块为 100 ms。Bot 另有每局共享的 2000 ms 时间池。

天梯支持 27 种语言。单次上传一个 UTF-8 源文件，上限 256 KiB。弱、中、强三档官方练习 Bot 均提供可单独上传的 C++20 源码。

## 页面与示例

| 入口 | 内容 |
|---|---|
| [竞技场](./index.html) | 内置 AI 对战、官方回放、单步和调速、本地 JSONL 回放导入与导出 |
| [图解规则](./rules.html) | 对战规则、T-Spin 图例、旋转与踢墙、Bot 通信协议 |
| [天梯](./ladder.html) | 注册、上传 Bot、自定义对战、排位与回放 |
| [Python 起步示例](./examples/starter_bot.py) | 演示最小 JSONL 通信 |
| [弱基准 Bot](./examples/weak_bot.cpp) | C++20 单文件 |
| [中基准 Bot](./examples/medium_bot.cpp) | C++20 单文件 |
| [强基准 Bot](./examples/strong_bot.cpp) | C++20 单文件 |

## 发布与更新

本仓库发布原项目 `web/dist` 中的公开静态文件。GitHub Pages 从 `main` 分支的仓库根目录发布。页面通过 `index.html` 和 `ladder.html` 中的 `tetra-api-base` 配置连接线上天梯 API。

更新时同步源项目跟踪的公开文件，并保留生产 API 配置、`.nojekyll` 和本 README。页面、脚本、样式和下载链接使用相对路径。

## 本地预览

在仓库根目录运行以下命令，然后打开浏览器竞技场或规则页。

```bash
python -m http.server 8000
```

[打开本地竞技场](http://localhost:8000/)
