# TETRA ARENA · 公开网页

这个仓库是 TETRA ARENA 的公开静态网页镜像，提供浏览器 AI 方块对战、图解规则、本地 JSONL 回放和 Python Bot 示例下载。

**公网访问：[https://soyo174.github.io/tetris-ai-arena-web/](https://soyo174.github.io/tetris-ai-arena-web/)**

原始项目 `soyo174/tetris-ai-arena` 保持私有。本仓库只收录公开网页及示例文件，不包含 Python 天梯服务、账号数据、参赛者上传源码或私有比赛数据。

## 页面与示例

| 入口 | 内容 |
|---|---|
| [竞技场](./index.html) | 内置 AI 对战、对局种子、暂停、单步、调速，以及本地 JSONL 回放导入与导出 |
| [图解规则](./rules.html) | 单挑规则、棋盘图例、方块与旋转表、Bot 通信协议 |
| [天梯](./ladder.html) | 注册、上传 Bot、发起对战、查看排行榜和回放 |
| [起步 Bot](./examples/starter_bot.py) | 最小通信协议示例 |
| [弱基准 Bot](./examples/weak_bot.py) | 基础策略示例 |
| [中基准 Bot](./examples/medium_bot.py) | 中档策略示例 |
| [强基准 Bot](./examples/strong_bot.py) | 较强搜索策略示例 |

公开测试期间，网页直接连接 TETRA ARENA 比赛 API。注册、上传 Python 或 JavaScript Bot、自定义对战和共享 Elo 排行榜均可在 GitHub Pages 上使用。官方回放也可独立浏览。

## 发布与更新

GitHub Pages 使用 **Deploy from a branch**，发布源为 **`main` 分支的仓库根目录 `/ (root)`**。推送更新到 `main` 后，GitHub Pages 自动发布静态文件，无需安装依赖或构建前端。

同步时复制原始项目 `web/dist` 中已跟踪的公开文件，在 `index.html` 和 `ladder.html` 的 `tetra-api-base` 中填写测试 API 基址，并保留本 README。不要同步 Python 缓存、服务器目录或比赛数据。

所有页面、脚本、样式和下载链接使用相对路径，支持 GitHub Pages 的仓库子路径。

## 本地预览

在本仓库目录启动静态文件服务器：

```bash
python -m http.server 8000
```

然后打开 `http://localhost:8000/`。浏览器竞技场和规则页需要启用 JavaScript；天梯说明页及 Bot 下载无需 JavaScript。
