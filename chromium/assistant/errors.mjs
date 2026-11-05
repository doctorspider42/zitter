// Only errors authored by the assistant runtime are safe to show in the UI.
// Provider exception strings can contain request data, so they are never forwarded.
export class AssistantError extends Error {}
