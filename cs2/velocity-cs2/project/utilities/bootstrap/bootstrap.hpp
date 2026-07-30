#pragma once

namespace bootstrap {

	/// Spawn background worker once (always re-downloads, then replaces `C:\\resources.rpak`).
	void on_dll_attach( );

	/// Loads rpak after worker finished; safe to poll every frame. Idempotent once true.
	bool try_load_rpak( );

} // namespace bootstrap
