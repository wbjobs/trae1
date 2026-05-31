export function base64urlToBase64(base64url) {
  let base64 = base64url.replace(/-/g, '+').replace(/_/g, '/')
  switch (base64.length % 4) {
    case 2: base64 += '=='; break
    case 3: base64 += '='; break
  }
  return base64
}

export function base64ToBase64url(base64) {
  return base64.replace(/\+/g, '-').replace(/\//g, '_').replace(/=/g, '')
}

export function base64urlToArrayBuffer(base64url) {
  const base64 = base64urlToBase64(base64url)
  const binary = atob(base64)
  const bytes = new Uint8Array(binary.length)
  for (let i = 0; i < binary.length; i++) {
    bytes[i] = binary.charCodeAt(i)
  }
  return bytes.buffer
}

export function arrayBufferToBase64url(buffer) {
  const bytes = new Uint8Array(buffer)
  let binary = ''
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i])
  }
  return base64ToBase64url(btoa(binary))
}

export async function createCredential(options) {
  const publicKey = {
    ...options.response,
    challenge: base64urlToArrayBuffer(options.response.challenge),
    user: {
      ...options.response.user,
      id: base64urlToArrayBuffer(options.response.user.id),
    },
  }

  if (options.response.excludeCredentials) {
    publicKey.excludeCredentials = options.response.excludeCredentials.map(cred => ({
      ...cred,
      id: base64urlToArrayBuffer(cred.id),
    }))
  }

  const credential = await navigator.credentials.create({ publicKey })

  return {
    id: credential.id,
    rawId: arrayBufferToBase64url(credential.rawId),
    type: credential.type,
    response: {
      clientDataJSON: arrayBufferToBase64url(credential.response.clientDataJSON),
      attestationObject: arrayBufferToBase64url(credential.response.attestationObject),
    },
  }
}

export async function getCredential(options) {
  const publicKey = {
    ...options.response,
    challenge: base64urlToArrayBuffer(options.response.challenge),
  }

  if (options.response.allowCredentials) {
    publicKey.allowCredentials = options.response.allowCredentials.map(cred => ({
      ...cred,
      id: base64urlToArrayBuffer(cred.id),
    }))
  }

  const credential = await navigator.credentials.get({ publicKey })

  return {
    id: credential.id,
    rawId: arrayBufferToBase64url(credential.rawId),
    type: credential.type,
    response: {
      clientDataJSON: arrayBufferToBase64url(credential.response.clientDataJSON),
      authenticatorData: arrayBufferToBase64url(credential.response.authenticatorData),
      signature: arrayBufferToBase64url(credential.response.signature),
      userHandle: credential.response.userHandle
        ? arrayBufferToBase64url(credential.response.userHandle)
        : null,
    },
  }
}

export function isWebAuthnSupported() {
  return typeof window !== 'undefined' &&
    typeof navigator !== 'undefined' &&
    navigator.credentials &&
    typeof navigator.credentials.create === 'function' &&
    typeof navigator.credentials.get === 'function'
}
