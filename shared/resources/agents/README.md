# Agent logos

The marks Slippy's panel shows beside the call counts for whoever sent the
last call. Each file is named after the agent as `AgentName()` in
`Source/Mcp.cpp` reports it, with `_` for spaces (`VS_Code.svg`);
`tools/agent_logos.py` builds them in, and brand colors live in that script. Drop in another SVG (a single 24 x 24
path set) to add an agent.

Sources:

- [Lobe Icons](https://github.com/lobehub/lobe-icons) (MIT): Claude, Codex,
  ChatGPT, Cursor, Gemini, Qwen, Kimi, Grok, Copilot, Windsurf, Cline,
  Roo Code, OpenCode, Goose
- [Simple Icons](https://github.com/simple-icons/simple-icons) (CC0): VS Code

The logos are trademarks of their owners and are used here only to show
which agent is calling. Check each brand's guidelines before shipping
Slippy to others.
