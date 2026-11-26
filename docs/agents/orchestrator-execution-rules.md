# Historical AO orchestrator proposal — superseded

The [previous AO proposal](https://github.com/martinkoenig/mantis-studio/blob/5304d957d54018411b5bd0c25201e0eaf0db7b60/docs/agents/orchestrator-execution-rules.md) is retained in Git history for evidence, not as current operating instructions. Do not load or activate its role rules in Hermes.

Current repository-wide engineering rules are [AGENTS.md](../../AGENTS.md), [CONTRIBUTING.md](../../CONTRIBUTING.md) and the [issue and handoff policy](../engineering/knowledge-and-handoff.md). The separately authorized runtime configuration owns task admission, dispatch, resource limits, recovery and completion notifications; this repository does not install or activate runtime configuration. Independent review, required CI, branch protection and protected owner decisions continue to apply.
