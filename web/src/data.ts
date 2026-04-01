import mercuryTexture from "../../assets/textures/mercury.jpg";
import venusTexture from "../../assets/textures/venus.jpg";
import earthTexture from "../../assets/textures/earth.jpg";
import marsTexture from "../../assets/textures/mars.jpg";
import jupiterTexture from "../../assets/textures/jupiter.jpg";
import saturnTexture from "../../assets/textures/saturn.jpg";
import uranusTexture from "../../assets/textures/uranus.jpg";
import neptuneTexture from "../../assets/textures/neptune.jpg";
import sunTexture from "../../assets/textures/sun.jpg";
import saturnRingAlphaTexture from "../../assets/textures/saturn_ring_alpha.png";

export type PlanetDefinition = {
  name: string;
  orbitRadius: number;
  orbitEccentricity: number;
  orbitInclinationDeg: number;
  orbitAscendingNodeDeg: number;
  orbitPeriapsisDeg: number;
  size: number;
  color: [number, number, number];
  orbitDays: number;
  orbitPhaseDeg: number;
  axialTiltDeg: number;
  rotationPeriodDays: number;
  hasRing: boolean;
  ringColor: [number, number, number];
  ringInner: number;
  ringOuter: number;
  textureUrl: string;
};

export const PLANET_DEFINITIONS: PlanetDefinition[] = [
  {
    name: "Mercury",
    orbitRadius: 4.0,
    orbitEccentricity: 0.2056,
    orbitInclinationDeg: 7.0,
    orbitAscendingNodeDeg: 48.33,
    orbitPeriapsisDeg: 29.12,
    size: 0.15,
    color: [0.7, 0.7, 0.7],
    orbitDays: 88.0,
    orbitPhaseDeg: 10.0,
    axialTiltDeg: 0.03,
    rotationPeriodDays: 58.65,
    hasRing: false,
    ringColor: [1.0, 1.0, 1.0],
    ringInner: 0.0,
    ringOuter: 0.0,
    textureUrl: mercuryTexture,
  },
  {
    name: "Venus",
    orbitRadius: 5.8,
    orbitEccentricity: 0.0068,
    orbitInclinationDeg: 3.39,
    orbitAscendingNodeDeg: 76.68,
    orbitPeriapsisDeg: 54.88,
    size: 0.3,
    color: [0.9, 0.7, 0.3],
    orbitDays: 225.0,
    orbitPhaseDeg: 75.0,
    axialTiltDeg: 177.36,
    rotationPeriodDays: -243.02,
    hasRing: false,
    ringColor: [1.0, 1.0, 1.0],
    ringInner: 0.0,
    ringOuter: 0.0,
    textureUrl: venusTexture,
  },
  {
    name: "Earth",
    orbitRadius: 8.0,
    orbitEccentricity: 0.0167,
    orbitInclinationDeg: 0.0,
    orbitAscendingNodeDeg: -11.26,
    orbitPeriapsisDeg: 114.21,
    size: 0.32,
    color: [0.2, 0.5, 1.0],
    orbitDays: 365.0,
    orbitPhaseDeg: 135.0,
    axialTiltDeg: 23.44,
    rotationPeriodDays: 0.997,
    hasRing: false,
    ringColor: [1.0, 1.0, 1.0],
    ringInner: 0.0,
    ringOuter: 0.0,
    textureUrl: earthTexture,
  },
  {
    name: "Mars",
    orbitRadius: 10.5,
    orbitEccentricity: 0.0934,
    orbitInclinationDeg: 1.85,
    orbitAscendingNodeDeg: 49.58,
    orbitPeriapsisDeg: 286.5,
    size: 0.2,
    color: [0.9, 0.3, 0.2],
    orbitDays: 687.0,
    orbitPhaseDeg: 195.0,
    axialTiltDeg: 25.19,
    rotationPeriodDays: 1.03,
    hasRing: false,
    ringColor: [1.0, 1.0, 1.0],
    ringInner: 0.0,
    ringOuter: 0.0,
    textureUrl: marsTexture,
  },
  {
    name: "Jupiter",
    orbitRadius: 14.5,
    orbitEccentricity: 0.0489,
    orbitInclinationDeg: 1.3,
    orbitAscendingNodeDeg: 100.46,
    orbitPeriapsisDeg: 273.87,
    size: 0.9,
    color: [0.8, 0.6, 0.4],
    orbitDays: 4333.0,
    orbitPhaseDeg: 250.0,
    axialTiltDeg: 3.13,
    rotationPeriodDays: 0.41,
    hasRing: false,
    ringColor: [1.0, 1.0, 1.0],
    ringInner: 0.0,
    ringOuter: 0.0,
    textureUrl: jupiterTexture,
  },
  {
    name: "Saturn",
    orbitRadius: 19.0,
    orbitEccentricity: 0.0565,
    orbitInclinationDeg: 2.49,
    orbitAscendingNodeDeg: 113.67,
    orbitPeriapsisDeg: 339.39,
    size: 0.8,
    color: [0.9, 0.8, 0.5],
    orbitDays: 10759.0,
    orbitPhaseDeg: 305.0,
    axialTiltDeg: 26.73,
    rotationPeriodDays: 0.45,
    hasRing: true,
    ringColor: [0.85, 0.75, 0.55],
    ringInner: 1.0,
    ringOuter: 1.6,
    textureUrl: saturnTexture,
  },
  {
    name: "Uranus",
    orbitRadius: 23.0,
    orbitEccentricity: 0.0472,
    orbitInclinationDeg: 0.77,
    orbitAscendingNodeDeg: 74.01,
    orbitPeriapsisDeg: 96.73,
    size: 0.5,
    color: [0.5, 0.8, 0.9],
    orbitDays: 30687.0,
    orbitPhaseDeg: 15.0,
    axialTiltDeg: 97.77,
    rotationPeriodDays: -0.72,
    hasRing: false,
    ringColor: [1.0, 1.0, 1.0],
    ringInner: 0.0,
    ringOuter: 0.0,
    textureUrl: uranusTexture,
  },
  {
    name: "Neptune",
    orbitRadius: 27.5,
    orbitEccentricity: 0.0086,
    orbitInclinationDeg: 1.77,
    orbitAscendingNodeDeg: 131.78,
    orbitPeriapsisDeg: 273.19,
    size: 0.5,
    color: [0.2, 0.3, 0.9],
    orbitDays: 60190.0,
    orbitPhaseDeg: 100.0,
    axialTiltDeg: 28.32,
    rotationPeriodDays: 0.67,
    hasRing: false,
    ringColor: [1.0, 1.0, 1.0],
    ringInner: 0.0,
    ringOuter: 0.0,
    textureUrl: neptuneTexture,
  },
];

export const SUN = {
  radius: 1.5,
  color: [1.0, 0.85, 0.2] as [number, number, number],
  rotationPeriodDays: 24.47,
  textureUrl: sunTexture,
};

export const SATURN_RING_ALPHA_URL = saturnRingAlphaTexture;
