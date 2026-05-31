window.__TAURI__ = window.__TAURI__ || {};
window.__TAURI__.core = window.__TAURI__.core || {
    invoke: function (cmd, args) {
        return window.__TAURI_INTERNALS__.invoke(cmd, args);
    },
};
window.__TAURI__.event = window.__TAURI__.event || {
    listen: function (event, cb) {
        return window.__TAURI_INTERNALS__.listen(event, cb);
    },
    emit: function (event, payload) {
        return window.__TAURI_INTERNALS__.emit(event, payload);
    },
};
