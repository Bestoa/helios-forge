import { MathUtils, Vector3 } from "three";
import { PLANET_DEFINITIONS, type PlanetDefinition, SUN } from "./data";

const ORBIT_DAY_SCALE = 60.0;
const ROTATION_TIME_SCALE = 8.0;

export type OrbitFrame = {
  periapsisDir: Vector3;
  minorDir: Vector3;
};

export type PlanetState = PlanetDefinition & {
  orbitMeanAnomalyDeg: number;
  rotationAngleDeg: number;
  orbitFrame: OrbitFrame;
};

export type SolarSystemState = {
  planets: PlanetState[];
  sunRotationAngleDeg: number;
};

function mulberry32(seed: number): () => number {
  let value = seed >>> 0;
  return () => {
    value += 0x6d2b79f5;
    let t = value;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function degToRad(deg: number): number {
  return MathUtils.degToRad(deg);
}

function solveEccentricAnomaly(meanAnomaly: number, eccentricity: number): number {
  let eccentricAnomaly = eccentricity < 0.8 ? meanAnomaly : Math.PI;
  for (let i = 0; i < 6; i += 1) {
    const sinE = Math.sin(eccentricAnomaly);
    const cosE = Math.cos(eccentricAnomaly);
    const f = eccentricAnomaly - eccentricity * sinE - meanAnomaly;
    let fp = 1.0 - eccentricity * cosE;
    if (Math.abs(fp) < 1e-4) {
      fp = fp < 0 ? -1e-4 : 1e-4;
    }
    eccentricAnomaly -= f / fp;
  }
  return eccentricAnomaly;
}

function orbitFrameFromElements(
  ascendingNodeDeg: number,
  inclinationDeg: number,
  periapsisDeg: number,
): OrbitFrame {
  const ascendingNode = degToRad(ascendingNodeDeg);
  const inclination = degToRad(inclinationDeg);
  const periapsis = degToRad(periapsisDeg);

  const cosNode = Math.cos(ascendingNode);
  const sinNode = Math.sin(ascendingNode);
  const cosInc = Math.cos(inclination);
  const sinInc = Math.sin(inclination);
  const cosPeri = Math.cos(periapsis);
  const sinPeri = Math.sin(periapsis);

  const periapsisDir = new Vector3(
    cosNode * cosPeri - sinNode * sinPeri * cosInc,
    sinPeri * sinInc,
    sinNode * cosPeri + cosNode * sinPeri * cosInc,
  ).normalize();

  const minorDir = new Vector3(
    -cosNode * sinPeri - sinNode * cosPeri * cosInc,
    cosPeri * sinInc,
    -sinNode * sinPeri + cosNode * cosPeri * cosInc,
  ).normalize();

  return { periapsisDir, minorDir };
}

export function createSolarSystemState(): SolarSystemState {
  const random = mulberry32(42);
  const planets = PLANET_DEFINITIONS.map((planet) => ({
    ...planet,
    orbitMeanAnomalyDeg: planet.orbitPhaseDeg,
    rotationAngleDeg: random() * 360.0,
    orbitFrame: orbitFrameFromElements(
      planet.orbitAscendingNodeDeg,
      planet.orbitInclinationDeg,
      planet.orbitPeriapsisDeg,
    ),
  }));

  return {
    planets,
    sunRotationAngleDeg: random() * 360.0,
  };
}

export function planetPosition(planet: PlanetState): Vector3 {
  const meanAnomaly = degToRad(planet.orbitMeanAnomalyDeg);
  const eccentricAnomaly = solveEccentricAnomaly(meanAnomaly, planet.orbitEccentricity);
  const cosE = Math.cos(eccentricAnomaly);
  const sinE = Math.sin(eccentricAnomaly);
  const radius = planet.orbitRadius * (1.0 - planet.orbitEccentricity * cosE);
  const trueAnomaly = Math.atan2(
    Math.sqrt(1.0 - planet.orbitEccentricity * planet.orbitEccentricity) * sinE,
    cosE - planet.orbitEccentricity,
  );

  return new Vector3()
    .addScaledVector(planet.orbitFrame.periapsisDir, radius * Math.cos(trueAnomaly))
    .addScaledVector(planet.orbitFrame.minorDir, radius * Math.sin(trueAnomaly));
}

export function createOrbitPoints(planet: PlanetState, segments = 192): Vector3[] {
  const points: Vector3[] = [];
  const semiLatusRectum =
    planet.orbitRadius * (1.0 - planet.orbitEccentricity * planet.orbitEccentricity);

  for (let i = 0; i < segments; i += 1) {
    const trueAnomaly = (Math.PI * 2.0 * i) / segments;
    const radius =
      semiLatusRectum / (1.0 + planet.orbitEccentricity * Math.cos(trueAnomaly));
    points.push(
      new Vector3()
        .addScaledVector(planet.orbitFrame.periapsisDir, radius * Math.cos(trueAnomaly))
        .addScaledVector(planet.orbitFrame.minorDir, radius * Math.sin(trueAnomaly)),
    );
  }

  return points;
}

export function updateSolarSystem(state: SolarSystemState, deltaSeconds: number, speed: number): void {
  for (const planet of state.planets) {
    if (planet.orbitDays > 0.0) {
      const angularSpeed = 360.0 / planet.orbitDays;
      planet.orbitMeanAnomalyDeg =
        (planet.orbitMeanAnomalyDeg + angularSpeed * deltaSeconds * speed * ORBIT_DAY_SCALE) % 360.0;
    }
    if (Math.abs(planet.rotationPeriodDays) > 1e-6) {
      let rotationSpeed = 360.0 / (Math.abs(planet.rotationPeriodDays) * ROTATION_TIME_SCALE);
      if (planet.rotationPeriodDays < 0.0) {
        rotationSpeed = -rotationSpeed;
      }
      planet.rotationAngleDeg =
        (planet.rotationAngleDeg + rotationSpeed * deltaSeconds * speed) % 360.0;
    }
  }

  const sunRotationSpeed = 360.0 / (SUN.rotationPeriodDays * ROTATION_TIME_SCALE);
  state.sunRotationAngleDeg =
    (state.sunRotationAngleDeg + sunRotationSpeed * deltaSeconds * speed) % 360.0;
}

export function createStarField(count: number, radius: number): {
  positions: Float32Array;
  brightness: Float32Array;
} {
  const random = mulberry32(42);
  const positions = new Float32Array(count * 3);
  const brightness = new Float32Array(count);

  for (let i = 0; i < count; i += 1) {
    const u = random() * 2.0 - 1.0;
    const theta = random() * Math.PI * 2.0;
    const r = radius * (0.55 + random() * 0.45);
    const xy = Math.sqrt(Math.max(0.0, 1.0 - u * u));
    positions[i * 3] = Math.cos(theta) * xy * r;
    positions[i * 3 + 1] = u * r;
    positions[i * 3 + 2] = Math.sin(theta) * xy * r;
    brightness[i] = 0.35 + random() * 0.65;
  }

  return { positions, brightness };
}
