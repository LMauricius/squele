# [Bug] A defect in existing code
---
$meta:
  color: '#ff4e3a'
Severity:
  type: string
  enum: [build, compile, latent, ub]
  description: build = default build fails; compile = header fails standalone; latent = fails on template instantiation; ub = undefined behavior at runtime
File:
  type: string
  description: Path relative to the repository root
---

Blocks:
- [Bug] Blocked by

# [Feature] A planned capability, written by the maintainer; its subtasks are Tasks
---
$meta:
  color: '#d9ff00'
---

Blocks:
- [Feature] Blocked by

Useful:
- [Note] Useful for

# [Optimization] A possible later optimization: a short description linking Notes and Hazards; its subtasks are Tasks (concrete steps and open ideas); kept in `Optimizations/`
---
$meta:
  color: '#00c8a0'
---

Addresses:
- [Hazard] Addressed by

Useful:
- [Note] Useful for

# [Hazard] A risk in existing code (speed, memory, safety), with a Severity section and a Possible efforts section; kept in `Hazards/`
---
$meta:
  color: '#ff9f1a'
---

Addressed by:
- [Optimization] Addresses

Useful:
- [Note] Useful for

# [Task] A concrete implementation step, written when implementation starts
---
$meta:
  color: '#0373fc'
---

Blocks:
- [Task] Blocked by

Fixes:
- [Bug] Fixed by

Useful:
- [Note] Useful for

# [Note] Lasting knowledge (design rules, conventions, pitfalls) that outlives a single Feature; kept in `Notes/`
---
$meta:
  color: '#9b59d0'
---
