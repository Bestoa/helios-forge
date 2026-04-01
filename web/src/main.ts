import "./style.css";
import {
  AdditiveBlending,
  BufferAttribute,
  BufferGeometry,
  CanvasTexture,
  Clock,
  Color,
  DoubleSide,
  Group,
  Line,
  LineBasicMaterial,
  LineLoop,
  MathUtils,
  Mesh,
  ShaderMaterial,
  PerspectiveCamera,
  Points,
  PointsMaterial,
  RepeatWrapping,
  RingGeometry,
  SRGBColorSpace,
  Scene,
  SphereGeometry,
  TextureLoader,
  Vector2,
  Vector3,
  WebGLRenderer,
} from "three";
import { PLANET_DEFINITIONS, SATURN_RING_ALPHA_URL, SUN } from "./data";
import {
  createOrbitPoints,
  createSolarSystemState,
  createStarField,
  planetPosition,
  updateSolarSystem,
  type PlanetState,
  type SolarSystemState,
} from "./simulation";

type PlanetVisual = {
  state: PlanetState;
  group: Group;
  mesh: Mesh;
  material: ShaderMaterial;
  ring?: Mesh;
  ringMaterial?: ShaderMaterial;
};

type InputState = {
  dragging: boolean;
  movedDuringDrag: boolean;
  lastMouse: Vector2;
  yawDeg: number;
  pitchDeg: number;
  zoomDelta: number;
  keys: Set<string>;
};

const app = document.querySelector<HTMLDivElement>("#app");

if (!app) {
  throw new Error("App container not found.");
}

const renderer = new WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setClearColor(new Color("#02050d"));
app.append(renderer.domElement);

const hud = document.createElement("aside");
hud.className = "hud";
app.append(hud);

const scene = new Scene();
scene.background = new Color("#02050d");

const camera = new PerspectiveCamera(45, window.innerWidth / window.innerHeight, 0.5, 500);
camera.position.set(0, 12, 60);

const loader = new TextureLoader();
const lightPosition = new Vector3(0, 0, 0);
const lightAmbient = new Color(0.02, 0.02, 0.02);
const lightDiffuse = new Color(1.34, 1.26, 1.16);
const lightSpecular = new Color(0.32, 0.32, 0.32);

const systemState = createSolarSystemState();
const planetVisuals: PlanetVisual[] = [];
let speed = 1.0;
let paused = false;
let cameraLocked = false;
let targetIndex = 0;

const input: InputState = {
  dragging: false,
  movedDuringDrag: false,
  lastMouse: new Vector2(),
  yawDeg: -90,
  pitchDeg: -20,
  zoomDelta: 0,
  keys: new Set(),
};

const moveDirection = new Vector3();
const cameraForward = new Vector3();
const cameraRight = new Vector3();
const cameraUp = new Vector3(0, 1, 0);
const cameraTarget = new Vector3();

const sunTexture = loader.load(SUN.textureUrl);
sunTexture.colorSpace = SRGBColorSpace;
const planetVertexShader = `
varying vec3 v_position;
varying vec3 v_normal;
varying vec2 v_texcoord;

void main() {
  vec4 world_pos = modelMatrix * vec4(position, 1.0);
  v_position = world_pos.xyz;
  v_normal = normalize(transpose(inverse(mat3(modelMatrix))) * normal);
  v_texcoord = uv;
  gl_Position = projectionMatrix * viewMatrix * world_pos;
}
`;

const planetFragmentShader = `
uniform vec3 u_light_pos;
uniform vec3 u_camera_pos;
uniform vec3 u_light_ambient;
uniform vec3 u_light_diffuse;
uniform vec3 u_light_specular;
uniform vec3 u_material_ambient;
uniform vec3 u_material_diffuse;
uniform vec3 u_material_specular;
uniform float u_material_shininess;
uniform float u_emission;
uniform sampler2D u_texture;
uniform float u_texture_mix;

varying vec3 v_position;
varying vec3 v_normal;
varying vec2 v_texcoord;

void main() {
  vec3 N = normalize(v_normal);
  vec3 light_vector = u_light_pos - v_position;
  float light_distance = max(length(light_vector), 0.001);
  vec3 L = light_vector / light_distance;
  vec3 V = normalize(u_camera_pos - v_position);
  vec3 R = reflect(-L, N);
  vec3 sampled_color = texture2D(u_texture, v_texcoord).rgb;
  vec3 texture_albedo = sampled_color * u_material_diffuse;
  vec3 base_color = mix(u_material_diffuse, texture_albedo, u_texture_mix);
  float attenuation = 1.0 / (1.0 + 0.00035 * light_distance * light_distance);

  vec3 ambient = u_light_ambient * mix(u_material_ambient, base_color, u_texture_mix);

  float ndotl = max(dot(N, L), 0.0);
  float sunlight = smoothstep(0.08, 0.28, ndotl) * ndotl;
  vec3 diffuse = sunlight * attenuation * u_light_diffuse * base_color;

  float spec = sunlight > 0.0 ? pow(max(dot(V, R), 0.0), u_material_shininess) : 0.0;
  spec *= smoothstep(0.22, 0.55, ndotl);
  vec3 specular = spec * attenuation * u_light_specular * u_material_specular;

  vec3 emission = u_emission * base_color;
  vec3 result = ambient + diffuse + specular + emission;
  gl_FragColor = vec4(result, 1.0);
  #include <tonemapping_fragment>
  #include <colorspace_fragment>
}
`;

const ringVertexShader = `
varying vec3 v_position;
varying vec3 v_normal;
varying vec2 v_texcoord;

void main() {
  vec4 world_pos = modelMatrix * vec4(position, 1.0);
  v_position = world_pos.xyz;
  v_normal = normalize(transpose(inverse(mat3(modelMatrix))) * normal);
  v_texcoord = uv;
  gl_Position = projectionMatrix * viewMatrix * world_pos;
}
`;

const ringFragmentShader = `
uniform vec3 u_light_pos;
uniform vec3 u_light_diffuse;
uniform vec3 u_material_color;
uniform float u_alpha;
uniform sampler2D u_alpha_texture;
uniform float u_use_alpha_texture;

varying vec3 v_position;
varying vec3 v_normal;
varying vec2 v_texcoord;

void main() {
  vec3 N = normalize(v_normal);
  vec3 light_vector = u_light_pos - v_position;
  float light_distance = max(length(light_vector), 0.001);
  vec3 L = light_vector / light_distance;
  float diff = max(dot(N, L), 0.0);
  float attenuation = 1.0 / (1.0 + 0.00035 * light_distance * light_distance);
  vec3 color = (0.12 + 0.88 * diff * attenuation) * u_material_color;
  float alpha_sample = texture2D(u_alpha_texture, v_texcoord).r;
  float alpha = u_alpha * mix(1.0, alpha_sample, u_use_alpha_texture);
  gl_FragColor = vec4(color, alpha);
}
`;

function createPlanetShaderMaterial(
  texture: ReturnType<TextureLoader["load"]>,
  colorValue: [number, number, number],
  emission: number,
  shininess: number,
  specularStrength: number,
  ambientStrength: [number, number, number],
): ShaderMaterial {
  return new ShaderMaterial({
    uniforms: {
      u_light_pos: { value: lightPosition.clone() },
      u_camera_pos: { value: camera.position.clone() },
      u_light_ambient: { value: lightAmbient.clone() },
      u_light_diffuse: { value: lightDiffuse.clone() },
      u_light_specular: { value: lightSpecular.clone() },
      u_material_ambient: { value: new Color(...ambientStrength) },
      u_material_diffuse: { value: new Color(...colorValue) },
      u_material_specular: { value: new Color(specularStrength, specularStrength, specularStrength) },
      u_material_shininess: { value: shininess },
      u_emission: { value: emission },
      u_texture: { value: texture },
      u_texture_mix: { value: 1.0 },
    },
    vertexShader: planetVertexShader,
    fragmentShader: planetFragmentShader,
    toneMapped: true,
  });
}

function createRingShaderMaterial(alphaTexture: ReturnType<TextureLoader["load"]>, colorValue: [number, number, number]): ShaderMaterial {
  return new ShaderMaterial({
    uniforms: {
      u_light_pos: { value: lightPosition.clone() },
      u_light_diffuse: { value: lightDiffuse.clone() },
      u_material_color: { value: new Color(...colorValue) },
      u_alpha: { value: 0.8 },
      u_alpha_texture: { value: alphaTexture },
      u_use_alpha_texture: { value: 1.0 },
    },
    vertexShader: ringVertexShader,
    fragmentShader: ringFragmentShader,
    transparent: true,
    side: DoubleSide,
    depthWrite: false,
    toneMapped: true,
  });
}

const sunMaterial = createPlanetShaderMaterial(sunTexture, SUN.color, 1.0, 1.0, 0.0, [0.0, 0.0, 0.0]);
sunMaterial.uniforms.u_light_ambient.value.setRGB(0.0, 0.0, 0.0);
sunMaterial.uniforms.u_light_diffuse.value.setRGB(0.0, 0.0, 0.0);
sunMaterial.uniforms.u_light_specular.value.setRGB(0.0, 0.0, 0.0);
const sunMesh = new Mesh(new SphereGeometry(SUN.radius, 40, 28), sunMaterial);
scene.add(sunMesh);

for (const planetState of systemState.planets) {
  const texture = loader.load(planetState.textureUrl);
  texture.colorSpace = SRGBColorSpace;
  const material = createPlanetShaderMaterial(texture, planetState.color, 0.0, 10.0, 0.01, planetState.color);
  const geometry = new SphereGeometry(planetState.size, 40, 28);
  const mesh = new Mesh(geometry, material);
  const group = new Group();
  group.add(mesh);

  let ring: Mesh | undefined;
  let ringMaterial: ShaderMaterial | undefined;
  if (planetState.hasRing) {
    const alphaTexture = loader.load(SATURN_RING_ALPHA_URL);
    alphaTexture.wrapS = RepeatWrapping;
    alphaTexture.wrapT = RepeatWrapping;
    const ringGeometry = new RingGeometry(
      planetState.ringInner * planetState.size,
      planetState.ringOuter * planetState.size,
      96,
      1,
    );
    ringMaterial = createRingShaderMaterial(alphaTexture, planetState.ringColor);
    ring = new Mesh(ringGeometry, ringMaterial);
    ring.rotation.x = Math.PI / 2;
    group.add(ring);
  }

  scene.add(group);
  planetVisuals.push({ state: planetState, group, mesh, material, ring, ringMaterial });

  const orbitGeometry = new BufferGeometry().setFromPoints(createOrbitPoints(planetState));
  const orbitMaterial = new LineBasicMaterial({ color: new Color(0.25, 0.25, 0.35), transparent: true, opacity: 0.95 });
  scene.add(new LineLoop(orbitGeometry, orbitMaterial));
}

const starFieldData = createStarField(1800, 220.0);
const starGeometry = new BufferGeometry();
starGeometry.setAttribute("position", new BufferAttribute(starFieldData.positions, 3));
starGeometry.setAttribute("brightness", new BufferAttribute(starFieldData.brightness, 1));

const starTextureCanvas = document.createElement("canvas");
starTextureCanvas.width = 64;
starTextureCanvas.height = 64;
const starContext = starTextureCanvas.getContext("2d");
if (!starContext) {
  throw new Error("Failed to create star texture.");
}
const gradient = starContext.createRadialGradient(32, 32, 0, 32, 32, 32);
gradient.addColorStop(0.0, "rgba(255,255,255,1)");
gradient.addColorStop(0.25, "rgba(255,255,255,0.9)");
gradient.addColorStop(1.0, "rgba(255,255,255,0)");
starContext.fillStyle = gradient;
starContext.fillRect(0, 0, 64, 64);
const starTexture = new CanvasTexture(starTextureCanvas);

const starMaterial = new PointsMaterial({
  map: starTexture,
  color: new Color("#dfe9ff"),
  size: 1.15,
  sizeAttenuation: true,
  transparent: true,
  opacity: 0.9,
  depthWrite: false,
  blending: AdditiveBlending,
});

const stars = new Points(starGeometry, starMaterial);
scene.add(stars);

function applyCameraOrientation(): void {
  const yawRad = MathUtils.degToRad(input.yawDeg);
  const pitchRad = MathUtils.degToRad(input.pitchDeg);
  cameraForward.set(
    Math.cos(pitchRad) * Math.cos(yawRad),
    Math.sin(pitchRad),
    Math.cos(pitchRad) * Math.sin(yawRad),
  ).normalize();
  cameraRight.crossVectors(cameraForward, cameraUp).normalize();
}

function updateCamera(deltaSeconds: number): void {
  applyCameraOrientation();

  moveDirection.set(0, 0, 0);
  if (input.keys.has("KeyW")) {
    moveDirection.add(cameraForward);
  }
  if (input.keys.has("KeyS")) {
    moveDirection.sub(cameraForward);
  }
  if (input.keys.has("KeyA")) {
    moveDirection.sub(cameraRight);
  }
  if (input.keys.has("KeyD")) {
    moveDirection.add(cameraRight);
  }
  if (moveDirection.lengthSq() > 0.0) {
    moveDirection.normalize().multiplyScalar(24.0 * deltaSeconds);
    camera.position.add(moveDirection);
  }

  if (Math.abs(input.zoomDelta) > 1e-6) {
    camera.position.addScaledVector(cameraForward, input.zoomDelta * 2.0);
    input.zoomDelta = 0.0;
  }

  if (cameraLocked) {
    camera.lookAt(cameraTarget);
  } else {
    camera.lookAt(camera.position.clone().add(cameraForward));
  }
}

function updateSceneFromState(state: SolarSystemState): void {
  sunMesh.rotation.y = MathUtils.degToRad(state.sunRotationAngleDeg);
  for (const visual of planetVisuals) {
    const position = planetPosition(visual.state);
    visual.group.position.copy(position);
    visual.group.rotation.z = MathUtils.degToRad(visual.state.axialTiltDeg);
    visual.mesh.rotation.y = MathUtils.degToRad(visual.state.rotationAngleDeg);
  }

  const focusVisual = targetIndex > 0 ? planetVisuals[targetIndex - 1] : undefined;
  if (focusVisual) {
    cameraTarget.copy(focusVisual.group.position);
  } else {
    cameraTarget.set(0, 0, 0);
  }
}

function updateLightingUniforms(): void {
  sunMaterial.uniforms.u_camera_pos.value.copy(camera.position);
  for (const visual of planetVisuals) {
    visual.material.uniforms.u_camera_pos.value.copy(camera.position);
    visual.material.uniforms.u_light_pos.value.copy(lightPosition);
    if (visual.ringMaterial) {
      visual.ringMaterial.uniforms.u_light_pos.value.copy(lightPosition);
    }
  }
}

function updateHud(frameFps: number): void {
  const focusName = targetIndex === 0 ? "Sun" : PLANET_DEFINITIONS[targetIndex - 1].name;
  hud.innerHTML = `
    <div class="hud__brand">Helios Forge Web</div>
    <div class="hud__row"><span>FPS</span><strong>${frameFps.toFixed(1)}</strong></div>
    <div class="hud__row"><span>Speed</span><strong>${speed.toFixed(1)}x${paused ? " · PAUSED" : ""}</strong></div>
    <div class="hud__row"><span>Camera</span><strong>${cameraLocked ? "LOCKED" : "FREE"}</strong></div>
    <div class="hud__row"><span>Focus</span><strong>${focusName}</strong></div>
    <div class="hud__hint">Drag rotate · Wheel zoom · WASD move</div>
    <div class="hud__hint">L lock · 0-8 focus · +/- speed · Space pause</div>
  `;
}

renderer.domElement.addEventListener("pointerdown", (event) => {
  if (event.button !== 0) {
    return;
  }
  input.dragging = true;
  input.movedDuringDrag = false;
  input.lastMouse.set(event.clientX, event.clientY);
  renderer.domElement.setPointerCapture(event.pointerId);
});

renderer.domElement.addEventListener("pointermove", (event) => {
  if (!input.dragging) {
    return;
  }

  const dx = event.clientX - input.lastMouse.x;
  const dy = event.clientY - input.lastMouse.y;
  if (Math.abs(dx) > 0 || Math.abs(dy) > 0) {
    input.movedDuringDrag = true;
  }
  input.yawDeg += dx * 0.2;
  input.pitchDeg = MathUtils.clamp(input.pitchDeg - dy * 0.2, -89.0, 89.0);
  input.lastMouse.set(event.clientX, event.clientY);
});

renderer.domElement.addEventListener("pointerup", (event) => {
  input.dragging = false;
  renderer.domElement.releasePointerCapture(event.pointerId);
});

renderer.domElement.addEventListener(
  "wheel",
  (event) => {
    event.preventDefault();
    input.zoomDelta += event.deltaY < 0 ? 1.0 : -1.0;
  },
  { passive: false },
);

function speedStepUp(currentSpeed: number): number {
  if (currentSpeed < 1.0) {
    return Math.min(1.0, Number((currentSpeed + 0.1).toFixed(1)));
  }
  if (currentSpeed < 10.0) {
    return Math.min(10.0, currentSpeed + 0.5);
  }
  return Math.min(50.0, currentSpeed + 1.0);
}

function speedStepDown(currentSpeed: number): number {
  if (currentSpeed <= 0.1) {
    return 0.1;
  }
  if (currentSpeed <= 1.0) {
    return Math.max(0.1, Number((currentSpeed - 0.1).toFixed(1)));
  }
  if (currentSpeed <= 10.0) {
    return Math.max(1.0, currentSpeed - 0.5);
  }
  return Math.max(10.0, currentSpeed - 1.0);
}

window.addEventListener("keydown", (event) => {
  if (["Space", "Equal", "Minus", "NumpadAdd", "NumpadSubtract"].includes(event.code)) {
    event.preventDefault();
  }

  if (event.repeat) {
    input.keys.add(event.code);
    return;
  }

  switch (event.code) {
    case "Space":
      paused = !paused;
      return;
    case "KeyL":
      cameraLocked = !cameraLocked;
      return;
    case "Equal":
    case "NumpadAdd":
      speed = speedStepUp(speed);
      return;
    case "Minus":
    case "NumpadSubtract":
      speed = speedStepDown(speed);
      return;
    case "Digit0":
      targetIndex = 0;
      return;
    default:
      break;
  }

  if (/^Digit[1-8]$/.test(event.code)) {
    targetIndex = Number.parseInt(event.code.replace("Digit", ""), 10);
    return;
  }

  input.keys.add(event.code);
});

window.addEventListener("keyup", (event) => {
  input.keys.delete(event.code);
});

window.addEventListener("blur", () => {
  input.keys.clear();
  input.dragging = false;
});

window.addEventListener("resize", () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

const clock = new Clock();

updateSceneFromState(systemState);
updateCamera(0);

function frame(): void {
  const deltaSeconds = Math.min(clock.getDelta(), 0.05);
  if (!paused) {
    updateSolarSystem(systemState, deltaSeconds, speed);
  }

  updateSceneFromState(systemState);
  updateCamera(deltaSeconds);
  updateLightingUniforms();
  const fps = deltaSeconds > 0 ? 1.0 / deltaSeconds : 0.0;
  updateHud(fps);

  renderer.render(scene, camera);
  requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
