// Rust core of the plugin. Deliberately dependency-free: it proves the ABI,
// not the ecosystem.
//
// Everything here is `extern "C"` / `#[repr(C)]`. No Qt type and no Rust type
// with a layout the C++ side does not control crosses the boundary.

use std::ffi::c_char;
use parking_lot::Mutex;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};

pub const TDP_PLUGIN_ABI_MAJOR: u32 = 1;
pub const TDP_PLUGIN_ABI_MINOR: u32 = 0;

#[repr(C)]
pub struct TdpPluginInfo {
    pub abi_major: u32,
    pub abi_minor: u32,
    pub id: *const c_char,
    pub name: *const c_char,
    pub version: *const c_char,
    pub author: *const c_char,
    pub capabilities: u64,
}

pub const CAP_TRANSFORM_TEXT: u64 = 1 << 0;
pub const CAP_CONTEXT_MENU: u64 = 1 << 1;
pub const CAP_DELETED_MESSAGES: u64 = 1 << 2;

pub type TdpInitFn = extern "C" fn(*const std::ffi::c_void);
pub type TdpTransformTextFn =
    extern "C" fn(*const u8, i32, *mut u8, i32) -> i32;
pub type TdpContextMenuFn = extern "C" fn(*const c_char) -> *const c_char;
pub type TdpDeletedMessageFn = extern "C" fn(i64, i64, *const u8, i32);

#[repr(C)]
pub struct TdpPluginVTable {
    pub abi_major: u32,
    pub abi_minor: u32,
    pub init: Option<TdpInitFn>,
    pub transform_text: Option<TdpTransformTextFn>,
    pub context_menu: Option<TdpContextMenuFn>,
    pub deleted_message: Option<TdpDeletedMessageFn>,
}

// ---- plugin state -------------------------------------------------------

static INITIALIZED: AtomicBool = AtomicBool::new(false);
static TRANSFORM_CALLS: AtomicU64 = AtomicU64::new(0);
static DELETED_SEEN: AtomicU64 = AtomicU64::new(0);

// The AyuGram-shaped feature: a bounded in-memory sink for deleted messages.
// A real implementation would write into a sqlite/rusqlite table.
#[derive(Default)]
struct DeletedSink {
    entries: Vec<(i64, i64, Vec<u8>)>,
}

static SINK: Mutex<Option<DeletedSink>> = Mutex::new(None);

// A small, honest "message pipeline" hook: uppercase-free, but it does real
// work on the bytes so the test can assert a deterministic transformation.
// Rule: strip a leading "/spoiler " prefix and wrap the rest in "||".
fn transform_impl(input: &str) -> Option<String> {
    let rest = input.strip_prefix("/spoiler ")?;
    Some(format!("||{rest}||"))
}

// ---- C ABI exports ------------------------------------------------------

static INFO_ID: &[u8] = b"dev.example.rust-poc\0";
static INFO_NAME: &[u8] = b"Rust PoC Plugin\0";
static INFO_VERSION: &[u8] = b"0.1.0\0";
static INFO_AUTHOR: &[u8] = b"tdesktop-rust-research\0";

// FFI friction worth noting: a struct holding raw pointers is not `Sync`, so
// it cannot live in a `static` as-is. The wrapper is the standard fix and is
// sound here because the pointers target immutable `'static` data.
struct SyncInfo(TdpPluginInfo);
unsafe impl Sync for SyncInfo {}

static INFO: SyncInfo = SyncInfo(TdpPluginInfo {
    abi_major: TDP_PLUGIN_ABI_MAJOR,
    abi_minor: TDP_PLUGIN_ABI_MINOR,
    id: INFO_ID.as_ptr() as *const c_char,
    name: INFO_NAME.as_ptr() as *const c_char,
    version: INFO_VERSION.as_ptr() as *const c_char,
    author: INFO_AUTHOR.as_ptr() as *const c_char,
    capabilities: CAP_TRANSFORM_TEXT | CAP_CONTEXT_MENU | CAP_DELETED_MESSAGES,
});

#[no_mangle]
pub extern "C" fn tdp_plugin_info() -> *const TdpPluginInfo {
    &INFO.0
}

extern "C" fn tdp_init(_host_api: *const std::ffi::c_void) {
    *SINK.lock() = Some(DeletedSink::default());
    INITIALIZED.store(true, Ordering::SeqCst);
}

extern "C" fn tdp_transform_text(
    text: *const u8,
    text_len: i32,
    out: *mut u8,
    out_capacity: i32,
) -> i32 {
    TRANSFORM_CALLS.fetch_add(1, Ordering::SeqCst);
    if text.is_null() || out.is_null() || text_len < 0 || out_capacity <= 0 {
        return -1;
    }
    let bytes = unsafe { std::slice::from_raw_parts(text, text_len as usize) };
    let Ok(input) = std::str::from_utf8(bytes) else {
        return -1;
    };
    let Some(result) = transform_impl(input) else {
        return -1;
    };
    let result = result.as_bytes();
    if result.len() + 1 > out_capacity as usize {
        return -2;
    }
    unsafe {
        std::ptr::copy_nonoverlapping(result.as_ptr(), out, result.len());
        *out.add(result.len()) = 0;
    }
    result.len() as i32
}

static MENU_ITEMS: &[u8] = b"poc.spoiler\tWrap in spoiler\0poc.stats\tShow Rust stats\0";

extern "C" fn tdp_context_menu(_menu_kind: *const c_char) -> *const c_char {
    MENU_ITEMS.as_ptr() as *const c_char
}

extern "C" fn tdp_deleted_message(
    dialog_id: i64,
    message_id: i64,
    payload: *const u8,
    payload_len: i32,
) {
    DELETED_SEEN.fetch_add(1, Ordering::SeqCst);
    if payload.is_null() || payload_len < 0 {
        return;
    }
    let bytes = unsafe { std::slice::from_raw_parts(payload, payload_len as usize) };
    let mut guard = SINK.lock();
    if let Some(sink) = guard.as_mut() {
        sink.entries.push((dialog_id, message_id, bytes.to_vec()));
    }
}

static VTABLE: TdpPluginVTable = TdpPluginVTable {
    abi_major: TDP_PLUGIN_ABI_MAJOR,
    abi_minor: TDP_PLUGIN_ABI_MINOR,
    init: Some(tdp_init),
    transform_text: Some(tdp_transform_text),
    context_menu: Some(tdp_context_menu),
    deleted_message: Some(tdp_deleted_message),
};

#[no_mangle]
pub extern "C" fn tdp_plugin_vtable() -> *const TdpPluginVTable {
    &VTABLE
}

// Observable state for the test harness, reachable without touching Qt.
#[no_mangle]
pub extern "C" fn tdp_plugin_self_check() -> u64 {
    let initialized = INITIALIZED.load(Ordering::SeqCst) as u64;
    let calls = TRANSFORM_CALLS.load(Ordering::SeqCst);
    let deleted = DELETED_SEEN.load(Ordering::SeqCst);
    let sink_len = SINK
        .lock()
        .as_ref()
        .map(|s| s.entries.len() as u64)
        .unwrap_or(0);
    initialized | (calls << 1) | (deleted << 24) | (sink_len << 48)
}

// Panic containment probe. A Rust panic that unwinds out of an `extern "C"`
// function into C++ frames is undefined behaviour, so a real plugin ABI must
// either compile plugins with `panic = "abort"` or catch at every entry point.
// This one catches, and reports the panic instead of aborting the process.
#[no_mangle]
pub extern "C" fn tdp_panic_probe(should_panic: i32) -> i32 {
    let result = std::panic::catch_unwind(|| {
        if should_panic != 0 {
            panic!("deliberate plugin panic");
        }
        41
    });
    match result {
        Ok(value) => value + 1,
        Err(_) => -1,
    }
}

// Accessor used by the Qt shim so the two halves share one state machine.
pub fn transform_for_qt(input: &str) -> Option<String> {
    transform_impl(input)
}

pub fn self_check_bits() -> (bool, u64, u64, u64) {
    (
        INITIALIZED.load(Ordering::SeqCst),
        TRANSFORM_CALLS.load(Ordering::SeqCst),
        DELETED_SEEN.load(Ordering::SeqCst),
        SINK
            .lock()
            .as_ref()
            .map(|s| s.entries.len() as u64)
            .unwrap_or(0),
    )
}
