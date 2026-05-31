import { useEffect, useRef } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/examples/jsm/controls/OrbitControls.js";
import type { PointCloud } from "../types";

interface PointCloudViewerProps {
  pointCloud: PointCloud | null;
  colorMode: "height" | "original";
}

export function PointCloudViewer({ pointCloud, colorMode }: PointCloudViewerProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<THREE.Scene | null>(null);
  const cameraRef = useRef<THREE.PerspectiveCamera | null>(null);
  const rendererRef = useRef<THREE.WebGLRenderer | null>(null);
  const controlsRef = useRef<OrbitControls | null>(null);
  const pointsRef = useRef<THREE.Points | null>(null);
  const animationRef = useRef<number>(0);

  useEffect(() => {
    if (!containerRef.current) return;

    const container = containerRef.current;
    const width = container.clientWidth;
    const height = container.clientHeight;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x1a1a2e);
    sceneRef.current = scene;

    const camera = new THREE.PerspectiveCamera(75, width / height, 0.1, 1000);
    camera.position.set(2, 2, 3);
    camera.lookAt(0, 0, 0);
    cameraRef.current = camera;

    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(width, height);
    renderer.setPixelRatio(window.devicePixelRatio);
    container.appendChild(renderer.domElement);
    rendererRef.current = renderer;

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.05;
    controls.rotateSpeed = 0.5;
    controls.zoomSpeed = 0.8;
    controlsRef.current = controls;

    const ambientLight = new THREE.AmbientLight(0xffffff, 0.5);
    scene.add(ambientLight);

    const directionalLight = new THREE.DirectionalLight(0xffffff, 0.8);
    directionalLight.position.set(5, 10, 7);
    scene.add(directionalLight);

    const gridHelper = new THREE.GridHelper(4, 40, 0x0f3460, 0x1a1a2e);
    scene.add(gridHelper);

    const axesHelper = new THREE.AxesHelper(2);
    scene.add(axesHelper);

    const animate = () => {
      animationRef.current = requestAnimationFrame(animate);
      controls.update();
      renderer.render(scene, camera);
    };
    animate();

    const handleResize = () => {
      if (!containerRef.current) return;
      const w = containerRef.current.clientWidth;
      const h = containerRef.current.clientHeight;
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h);
    };
    window.addEventListener("resize", handleResize);

    return () => {
      window.removeEventListener("resize", handleResize);
      cancelAnimationFrame(animationRef.current);
      controls.dispose();
      renderer.dispose();
      if (container.contains(renderer.domElement)) {
        container.removeChild(renderer.domElement);
      }
    };
  }, []);

  useEffect(() => {
    if (!pointCloud || !sceneRef.current) return;

    if (pointsRef.current) {
      sceneRef.current.remove(pointsRef.current);
      pointsRef.current.geometry.dispose();
      if (Array.isArray(pointsRef.current.material)) {
        pointsRef.current.material.forEach((m) => m.dispose());
      } else {
        pointsRef.current.material.dispose();
      }
    }

    const geometry = new THREE.BufferGeometry();
    const positions = new Float32Array(pointCloud.points.length * 3);
    const colors = new Float32Array(pointCloud.points.length * 3);

    const bounds = pointCloud.bounds;
    const minZ = bounds.min.z;
    const maxZ = bounds.max.z;

    for (let i = 0; i < pointCloud.points.length; i++) {
      const point = pointCloud.points[i];
      positions[i * 3] = point.x;
      positions[i * 3 + 1] = point.y;
      positions[i * 3 + 2] = point.z;

      if (colorMode === "height" && maxZ > minZ) {
        const t = Math.max(0, Math.min(1, (point.z - minZ) / (maxZ - minZ)));
        const [r, g, b] = heightToRGB(t);
        colors[i * 3] = r;
        colors[i * 3 + 1] = g;
        colors[i * 3 + 2] = b;
      } else if (pointCloud.colors[i]) {
        const color = pointCloud.colors[i];
        colors[i * 3] = color.r / 255;
        colors[i * 3 + 1] = color.g / 255;
        colors[i * 3 + 2] = color.b / 255;
      } else {
        colors[i * 3] = 1;
        colors[i * 3 + 1] = 1;
        colors[i * 3 + 2] = 1;
      }
    }

    geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
    geometry.setAttribute("color", new THREE.BufferAttribute(colors, 3));
    geometry.computeBoundingSphere();

    const material = new THREE.PointsMaterial({
      size: 0.01,
      vertexColors: true,
      sizeAttenuation: true,
      transparent: true,
      opacity: 0.9,
    });

    const points = new THREE.Points(geometry, material);
    sceneRef.current.add(points);
    pointsRef.current = points;

    if (cameraRef.current && geometry.boundingSphere) {
      const center = geometry.boundingSphere.center;
      const radius = geometry.boundingSphere.radius;
      const distance = radius * 2;
      cameraRef.current.position.set(
        center.x + distance,
        center.y + distance,
        center.z + distance
      );
      cameraRef.current.lookAt(center);
      if (controlsRef.current) {
        controlsRef.current.target.copy(center);
        controlsRef.current.update();
      }
    }
  }, [pointCloud, colorMode]);

  return <div ref={containerRef} className="viewer-canvas" />;
}

function heightToRGB(t: number): [number, number, number] {
  const clamped = Math.max(0, Math.min(1, t));

  if (clamped < 0.25) {
    const lt = clamped / 0.25;
    return [0, lt, 0.5 - lt * 0.5];
  } else if (clamped < 0.5) {
    const lt = (clamped - 0.25) / 0.25;
    return [lt, 1, 0];
  } else if (clamped < 0.75) {
    const lt = (clamped - 0.5) / 0.25;
    return [1, 1 - lt, 0];
  } else {
    const lt = (clamped - 0.75) / 0.25;
    return [1, 0, lt];
  }
}
