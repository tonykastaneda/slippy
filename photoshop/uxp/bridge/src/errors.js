// One error shape for everything an agent sees: { code, message, hint }.

export class SlippyError extends Error {
	constructor(code, message, hint) {
		super(message);
		this.code = code;
		this.hint = hint;
	}
	toJSON() { return { code: this.code, message: this.message, ...(this.hint ? { hint: this.hint } : {}) }; }
}

export const fail = (code, message, hint) => new SlippyError(code, message, hint);

// An MCP tool result for a failure: readable text plus the structured envelope.
export function errorResult(e) {
	const err = e instanceof SlippyError ? e : fail("bridge_error", String(e?.message || e));
	return { isError: true, content: [{ type: "text", text: JSON.stringify(err.toJSON(), null, 1) }] };
}

export function okResult(value) {
	return { content: [{ type: "text", text: typeof value === "string" ? value : JSON.stringify(value, null, 1) }] };
}
