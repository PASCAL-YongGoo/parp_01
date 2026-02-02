# AI Agent Configuration (oh-my-opencode)

> 이 문서는 프로젝트에서 사용하는 AI 에이전트들의 모델 할당 현황을 기록합니다.
> 설정 파일: `~/.config/opencode/oh-my-opencode.json`

---

## Agent -> AI Model Mapping

### Core Agents

| Agent | AI Model | Provider | Variant | Purpose |
|-------|----------|----------|---------|---------|
| **sisyphus** | Claude Opus 4.5 | Amazon Bedrock | - | Main orchestrator (you're talking to this) |
| **prometheus** | Claude Opus 4.5 | Amazon Bedrock | - | Planning, strategy |
| **metis** | GPT-5.2 | OpenAI | high | Request analysis, ambiguity detection |
| **oracle** | GPT-5.2 | OpenAI | high | Complex debugging, architecture consulting |
| **momus** | Gemini 3 Pro | Google | high | Plan review, quality evaluation |
| **explore** | Claude Haiku 4.5 | Amazon Bedrock | - | Fast codebase search |
| **librarian** | Claude Haiku 4.5 | Amazon Bedrock | - | External docs, GitHub, OSS search |
| **multimodal-looker** | Gemini 3 Flash | Google | medium | Image/PDF/diagram analysis |
| **atlas** | Claude Haiku 4.5 | Amazon Bedrock | - | Additional exploration |

### Delegate Task Categories

| Category | AI Model | Provider | Variant | Purpose |
|----------|----------|----------|---------|---------|
| **visual-engineering** | Gemini 3 Pro | Google | high | Frontend, UI/UX, design, styling |
| **ultrabrain** | Claude Opus 4.5 | Amazon Bedrock | - | Complex logic, hard problems |
| **deep** | GPT-5.2 | OpenAI | medium | Goal-oriented autonomous problem solving |
| **artistry** | Gemini 3 Pro | Google | high | Creative, unconventional approaches |
| **quick** | Claude Haiku 4.5 | Amazon Bedrock | - | Simple tasks, quick fixes |
| **unspecified-low** | Claude Haiku 4.5 | Amazon Bedrock | - | Other simple tasks |
| **unspecified-high** | Claude Opus 4.5 | Amazon Bedrock | - | Other complex tasks |
| **writing** | Gemini 3 Flash | Google | medium | Documentation, technical writing |

---

## AI Models Summary

### By Provider

| Provider | Models Used | Agents |
|----------|-------------|--------|
| **Amazon Bedrock** | Claude Opus 4.5, Claude Haiku 4.5 | sisyphus, prometheus, explore, librarian, atlas, ultrabrain, quick, unspecified-* |
| **OpenAI** | GPT-5.2 | metis, oracle, deep |
| **Google** | Gemini 3 Pro, Gemini 3 Flash | momus, multimodal-looker, visual-engineering, artistry, writing |

### By Role

| Role | AI Models |
|------|-----------|
| **Main/Planning** | Claude Opus 4.5 |
| **Fast Search** | Claude Haiku 4.5 |
| **Analysis/Consulting** | GPT-5.2 |
| **Creative/Review** | Gemini 3 Pro |
| **Docs/Visual** | Gemini 3 Flash |

---

## Usage Guidelines

### When to Use Each Agent

| Task | Best Agent | AI Model |
|------|------------|----------|
| "Find where X is defined" | `explore` | Claude Haiku 4.5 |
| "How does library Y work?" | `librarian` | Claude Haiku 4.5 |
| "Debug this complex issue" | `oracle` | GPT-5.2 |
| "Analyze this request" | `metis` | GPT-5.2 |
| "Review this plan" | `momus` | Gemini 3 Pro |
| "Analyze this diagram" | `multimodal-looker` | Gemini 3 Flash |

### When to Use Each Category

| Task | Best Category | AI Model |
|------|---------------|----------|
| UI/UX work | `visual-engineering` | Gemini 3 Pro |
| Hard algorithm | `ultrabrain` | Claude Opus 4.5 |
| Autonomous fix | `deep` | GPT-5.2 |
| Creative solution | `artistry` | Gemini 3 Pro |
| Simple fix | `quick` | Claude Haiku 4.5 |
| Write docs | `writing` | Gemini 3 Flash |

---

## Parallel Execution Example

```
Complex feature implementation:

1. metis (GPT-5.2)           -> Request analysis
2. explore (Haiku) [parallel] -> Search existing patterns
   librarian (Haiku) [parallel] -> Search external docs
3. prometheus (Opus)         -> Create plan
4. momus (Gemini Pro)        -> Review plan
5. delegate_task(deep)       -> Implement (GPT-5.2)
6. oracle (GPT-5.2)          -> Final verification
```

---

## Config File Location

```
~/.config/opencode/oh-my-opencode.json
```

To modify agent assignments, edit this file and restart opencode.

---

## Last Updated

- **Date**: 2026-02-02
- **Session**: Agent configuration documentation
