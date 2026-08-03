// device.js — builds the schematic implosion device (museum-style, illustrative proportions)
// Everything is modeled from primitives: casing lathe, tail fins, the iconic
// soccer-ball HE lens array (12 pentagonal + 20 hexagonal blocks from an
// icosahedron's vertices and face centers), pusher, reflector, core, initiator.
import * as THREE from 'three';

export function buildDevice() {
  const root = new THREE.Group();
  const mats = [];            // all structural materials (for clipping planes)
  const layers = {};          // name -> Group
  const mkMat = (opts) => { const m = new THREE.MeshStandardMaterial(opts); mats.push(m); return m; };

  // ---------- casing (lathe, iconic egg silhouette) + tail fins ----------
  const casingG = new THREE.Group(); layers.casing = casingG;
  const prof = [
    [0.00, -1.62], [0.18, -1.60], [0.44, -1.50], [0.68, -1.30], [0.86, -1.02],
    [0.95, -0.66], [0.96, -0.28], [0.92,  0.10], [0.82,  0.44], [0.66,  0.72],
    [0.48,  0.94], [0.34,  1.10], [0.26,  1.22],
  ].map(([x, y]) => new THREE.Vector2(x, y));
  const casingMat = mkMat({ color: 0xd9a53a, metalness: 0.55, roughness: 0.38 });   // iconic mustard
  const casing = new THREE.Mesh(new THREE.LatheGeometry(prof, 64), casingMat);
  casing.castShadow = casing.receiveShadow = true;
  casingG.add(casing);
  // nose cap
  const nose = new THREE.Mesh(new THREE.SphereGeometry(0.18, 24, 16), mkMat({ color: 0x8a8f96, metalness: .8, roughness: .3 }));
  nose.position.y = -1.60; casingG.add(nose);
  // tail plate + 4 box fins
  const finMat = mkMat({ color: 0x2b2e33, metalness: 0.6, roughness: 0.45 });
  const plate = new THREE.Mesh(new THREE.CylinderGeometry(0.30, 0.34, 0.10, 32), finMat);
  plate.position.y = 1.26; casingG.add(plate);
  for (let i = 0; i < 4; i++) {
    const fin = new THREE.Mesh(new THREE.BoxGeometry(0.05, 0.52, 0.62), finMat);
    const a = i * Math.PI / 2 + Math.PI / 4;
    fin.position.set(Math.cos(a) * 0.36, 1.52, Math.sin(a) * 0.36);
    fin.rotation.y = -a + Math.PI / 2;
    fin.castShadow = true;
    casingG.add(fin);
  }
  root.add(casingG);

  // ---------- HE lens array: 12 pent + 20 hex blocks (soccer-ball pattern) ----------
  const lensG = new THREE.Group(); layers.lens = lensG;
  const blocks = [];
  const ico = new THREE.IcosahedronGeometry(1, 0);   // non-indexed: 60 verts (20 faces × 3)
  const pos = ico.getAttribute('position');
  const seen = new Set(); const pentDirs = [];
  for (let i = 0; i < pos.count; i++) {              // dedupe → the 12 unique icosa vertices
    const v = new THREE.Vector3().fromBufferAttribute(pos, i).normalize();
    const key = v.toArray().map(n => n.toFixed(3)).join(',');
    if (!seen.has(key)) { seen.add(key); pentDirs.push(v); }
  }
  const hexDirs = [];
  for (let i = 0; i < pos.count; i += 3) {
    const c = new THREE.Vector3().fromBufferAttribute(pos, i)
      .add(new THREE.Vector3().fromBufferAttribute(pos, i + 1))
      .add(new THREE.Vector3().fromBufferAttribute(pos, i + 2)).normalize();
    hexDirs.push(c);
  }
  const blockMatBase = { color: 0x6b6f4a, metalness: 0.25, roughness: 0.62, emissive: 0x000000, emissiveIntensity: 1 };
  const mkBlock = (dir, sides, idx) => {
    // tapered radial prism: cylinder with 5/6 radial segments
    const geo = new THREE.CylinderGeometry(0.155, 0.195, 0.30, sides, 1);
    const mat = new THREE.MeshStandardMaterial(blockMatBase); mats.push(mat);
    const m = new THREE.Mesh(geo, mat);
    const r0 = 0.66;
    m.position.copy(dir).multiplyScalar(r0);
    m.lookAt(0, 0, 0);                 // cylinder Y-axis now points at center
    m.rotateX(Math.PI / 2);            // lay the prism axis along the radius
    m.castShadow = true;
    m.userData = { dir: dir.clone(), r0, failed: false, idx, baseColor: 0x6b6f4a };
    lensG.add(m); blocks.push(m);
  };
  pentDirs.forEach((d, i) => mkBlock(d, 5, i));
  hexDirs.forEach((d, i) => mkBlock(d, 6, 12 + i));
  root.add(lensG);

  // ---------- pusher / tamper ----------
  const pusherMat = mkMat({ color: 0x9aa2ab, metalness: 0.85, roughness: 0.28 });
  const pusher = new THREE.Mesh(new THREE.SphereGeometry(0.48, 48, 32), pusherMat);
  pusher.castShadow = true;
  layers.pusher = new THREE.Group().add(pusher); root.add(layers.pusher);

  // ---------- reflector ----------
  const reflMat = mkMat({ color: 0xc7b9a0, metalness: 0.35, roughness: 0.5 });
  const reflector = new THREE.Mesh(new THREE.SphereGeometry(0.30, 48, 32), reflMat);
  layers.reflector = new THREE.Group().add(reflector); root.add(layers.reflector);

  // ---------- core (schematic) ----------
  const coreMat = mkMat({ color: 0x8f9299, metalness: 0.9, roughness: 0.22, emissive: 0xff5a1f, emissiveIntensity: 0.0 });
  const core = new THREE.Mesh(new THREE.SphereGeometry(0.17, 48, 32), coreMat);
  layers.core = new THREE.Group().add(core); root.add(layers.core);

  // ---------- initiator (schematic) ----------
  const initMat = mkMat({ color: 0x333944, metalness: 0.7, roughness: 0.4, emissive: 0x46d6ff, emissiveIntensity: 0.35 });
  const initiator = new THREE.Mesh(new THREE.SphereGeometry(0.05, 24, 16), initMat);
  layers.initiator = new THREE.Group().add(initiator); root.add(layers.initiator);

  // ---------- cradle ----------
  const cradleMat = mkMat({ color: 0x3a4048, metalness: 0.6, roughness: 0.5 });
  const ring = new THREE.Mesh(new THREE.TorusGeometry(0.72, 0.045, 12, 48), cradleMat);
  ring.rotation.x = Math.PI / 2; ring.position.y = -1.28; root.add(ring);
  for (let i = 0; i < 3; i++) {
    const a = i * Math.PI * 2 / 3;
    const leg = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.05, 0.9, 10), cradleMat);
    leg.position.set(Math.cos(a) * 0.72, -1.72, Math.sin(a) * 0.72);
    leg.rotation.z = Math.cos(a) * 0.35; leg.rotation.x = -Math.sin(a) * 0.35;
    root.add(leg);
  }

  // ---------- helpers ----------
  const shellHome = new Map([[pusher, 1], [reflector, 1], [core, 1]]);

  function setExplode(e) {
    casingG.position.y = e * 1.45;
    for (const b of blocks) b.position.copy(b.userData.dir).multiplyScalar(b.userData.r0 + e * 0.62);
    pusher.scale.setScalar(1 + e * 0.34);
    reflector.scale.setScalar(1 + e * 0.17);
    // core & initiator stay — the eye needs a fixed center
  }

  function setClipPlane(plane) {       // THREE.Plane or null — applied to all structural mats
    for (const m of mats) { m.clippingPlanes = plane ? [plane] : null; m.clipShadows = !!plane; }
  }

  function setLayerVisible(name, v) { if (layers[name]) layers[name].visible = v; }

  function setBlockFailed(mesh, failed) {
    mesh.userData.failed = failed;
    mesh.material.color.setHex(failed ? 0x4a2b28 : mesh.userData.baseColor);
    mesh.material.emissive.setHex(failed ? 0x330b06 : 0x000000);
    mesh.material.emissiveIntensity = failed ? 0.6 : 1;
  }

  const anchors = [
    { name: 'casing + tail fins', obj: casing,  off: new THREE.Vector3(0.95, 0.1, 0), layer: 'casing' },
    { name: 'HE lens array (32)', obj: blocks[7], off: new THREE.Vector3(0, 0.25, 0), layer: 'lens' },
    { name: 'pusher / tamper',    obj: pusher,  off: new THREE.Vector3(0.5, 0, 0),  layer: 'pusher' },
    { name: 'reflector',          obj: reflector, off: new THREE.Vector3(0.32, 0, 0), layer: 'reflector' },
    { name: 'core (schematic)',   obj: core,    off: new THREE.Vector3(0.2, 0, 0),  layer: 'core' },
    { name: 'initiator (schematic)', obj: initiator, off: new THREE.Vector3(0.1, -0.1, 0), layer: 'initiator' },
  ];

  return { root, blocks, parts: { casingG, pusher, reflector, core, initiator },
           mats, anchors, shellHome, setExplode, setClipPlane, setLayerVisible, setBlockFailed };
}
