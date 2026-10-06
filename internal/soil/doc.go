// Package soil estimates zone soil water from AZMET reference ET, rain-gauge
// increments, and completed/stopped run history. The estimate is display-only:
// it never pauses, skips, or starts watering. AZMET station ids stay in the
// private config file and are not log or API fields.
package soil
