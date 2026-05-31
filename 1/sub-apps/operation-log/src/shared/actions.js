let actions = null;

function setActions(newActions) {
  actions = newActions;
}

function getActions() {
  return actions;
}

function emitGlobalStateChange(state) {
  if (actions && actions.setGlobalState) {
    actions.setGlobalState(state);
  }
}

function onGlobalStateChange(callback, fireImmediately) {
  if (actions && actions.onGlobalStateChange) {
    actions.onGlobalStateChange(callback, fireImmediately);
  }
}

export default {
  setActions,
  getActions,
  emitGlobalStateChange,
  onGlobalStateChange
};
