import * as THREE from 'three'
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js'
import { STLLoader } from 'three/examples/jsm/loaders/STLLoader.js'

export class Viewer3D {
  private scene: THREE.Scene
  private camera: THREE.PerspectiveCamera
  private renderer: THREE.WebGLRenderer
  private controls: OrbitControls
  private container: HTMLElement
  private meshes: THREE.Mesh[] = []
  private rafId: number = 0

  constructor(container: HTMLElement) {
    this.container = container

    this.scene = new THREE.Scene()
    this.scene.background = new THREE.Color(0x1a1a1f)

    this.camera = new THREE.PerspectiveCamera(
      50,
      container.clientWidth / container.clientHeight,
      0.01,
      10000
    )
    this.camera.position.set(60, 60, 90)

    this.renderer = new THREE.WebGLRenderer({ antialias: true })
    this.renderer.setPixelRatio(window.devicePixelRatio)
    this.renderer.setSize(container.clientWidth, container.clientHeight)
    container.appendChild(this.renderer.domElement)

    const ambient = new THREE.AmbientLight(0xffffff, 0.55)
    this.scene.add(ambient)
    const dir = new THREE.DirectionalLight(0xffffff, 0.8)
    dir.position.set(100, 200, 150)
    this.scene.add(dir)
    const dir2 = new THREE.DirectionalLight(0x99bbff, 0.35)
    dir2.position.set(-100, -80, -120)
    this.scene.add(dir2)

    const grid = new THREE.GridHelper(200, 20, 0x444444, 0x2a2a30)
    ;(grid.material as THREE.Material).transparent = true
    ;(grid.material as THREE.Material).opacity = 0.5
    this.scene.add(grid)

    this.controls = new OrbitControls(this.camera, this.renderer.domElement)
    this.controls.enableDamping = true
    this.controls.dampingFactor = 0.08

    window.addEventListener('resize', this.onResize)
    this.animate()
  }

  private onResize = () => {
    const w = this.container.clientWidth
    const h = this.container.clientHeight
    this.camera.aspect = w / h
    this.camera.updateProjectionMatrix()
    this.renderer.setSize(w, h)
  }

  private animate = () => {
    this.rafId = requestAnimationFrame(this.animate)
    this.controls.update()
    this.renderer.render(this.scene, this.camera)
  }

  clear() {
    for (const m of this.meshes) {
      this.scene.remove(m)
      m.geometry.dispose()
      if (Array.isArray(m.material)) m.material.forEach((x) => x.dispose())
      else m.material.dispose()
    }
    this.meshes = []
  }

  loadSTL(buffer: ArrayBuffer, color: number, opacity: number = 1.0) {
    const loader = new STLLoader()
    const geom = loader.parse(buffer)
    geom.computeVertexNormals()
    const mat = new THREE.MeshPhongMaterial({
      color,
      specular: 0x222222,
      shininess: 40,
      transparent: opacity < 1,
      opacity,
      side: THREE.DoubleSide,
      flatShading: false
    })
    const mesh = new THREE.Mesh(geom, mat)
    mesh.castShadow = true
    mesh.receiveShadow = true
    this.scene.add(mesh)
    this.meshes.push(mesh)
    this.frame()
  }

  frame() {
    if (this.meshes.length === 0) return
    const box = new THREE.Box3()
    for (const m of this.meshes) box.expandByObject(m)
    const size = box.getSize(new THREE.Vector3())
    const center = box.getCenter(new THREE.Vector3())
    const maxDim = Math.max(size.x, size.y, size.z) || 1
    const fov = this.camera.fov * (Math.PI / 180)
    const dist = (maxDim / (2 * Math.tan(fov / 2))) * 1.8
    this.camera.position.set(center.x + dist, center.y + dist * 0.7, center.z + dist)
    this.camera.lookAt(center)
    this.controls.target.copy(center)
    this.controls.update()
  }

  dispose() {
    cancelAnimationFrame(this.rafId)
    window.removeEventListener('resize', this.onResize)
    this.clear()
    this.renderer.dispose()
    if (this.renderer.domElement.parentElement === this.container) {
      this.container.removeChild(this.renderer.domElement)
    }
  }
}
