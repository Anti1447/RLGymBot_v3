#!/usr/bin/env python3
"""
RocketSim Rotation Matrix Visualization Tool

Generates an interactive 3D visualization showing how car orientation vectors
(forward, right, up) change as the car rotates through yaw, pitch, and roll.

This tool mimics RocketSim's btMatrix3x3 rotation matrix system and helps
understand the coordinate system and validate rotation calculations.

Usage:
    python rotation_visualizer.py

Output:
    rotation_viewer.html - Interactive HTML visualization (open in browser)

Author: Claude
Date: 2026-03-07
"""

import numpy as np
import json
from pathlib import Path


class RotMat:
    """3x3 rotation matrix mimicking RocketSim's btMatrix3x3

    Basis vectors stored as rows:
    - forward: direction car is facing (row 0)
    - right: car's rightward direction (row 1)
    - up: car's upward direction (row 2)

    RocketSim uses Bullet Physics btMatrix3x3 with intrinsic ZYX Euler angles.
    """

    def __init__(self, forward, right, up):
        """Initialize RotMat with three basis vectors"""
        self.forward = np.array(forward, dtype=float)
        self.right = np.array(right, dtype=float)
        self.up = np.array(up, dtype=float)

    @staticmethod
    def from_euler_angles(yaw, pitch, roll):
        """Convert Euler angles (radians) to RotMat

        Convention: Intrinsic ZYX rotation order
        - Yaw: rotation around Z-axis (vertical, up vector)
        - Pitch: rotation around Y-axis (side-to-side tilt)
        - Roll: rotation around X-axis (barrel roll)

        This matches RocketSim's Angle(yaw, pitch, roll).ToRotMat()

        Args:
            yaw: rotation around Z-axis in radians (0 = facing +Y/orange goal)
            pitch: rotation around Y-axis in radians (0 = level)
            roll: rotation around X-axis in radians (0 = upright)

        Returns:
            RotMat instance with forward, right, up basis vectors

        Expected behaviors:
            - Yaw 0°:   forward=[0, 1, 0], right=[1, 0, 0], up=[0, 0, 1]
            - Yaw 90°:  forward=[1, 0, 0], right=[0, -1, 0], up=[0, 0, 1]
            - Yaw 180°: forward=[0, -1, 0], right=[-1, 0, 0], up=[0, 0, 1]
            - Yaw 270°: forward=[-1, 0, 0], right=[0, 1, 0], up=[0, 0, 1]
        """
        # Individual rotation matrices for each axis
        cos_y, sin_y = np.cos(yaw), np.sin(yaw)
        cos_p, sin_p = np.cos(pitch), np.sin(pitch)
        cos_r, sin_r = np.cos(roll), np.sin(roll)

        # Rz (yaw around Z-axis)
        Rz = np.array([
            [cos_y, -sin_y, 0],
            [sin_y, cos_y, 0],
            [0, 0, 1]
        ])

        # Ry (pitch around Y-axis)
        Ry = np.array([
            [cos_p, 0, sin_p],
            [0, 1, 0],
            [-sin_p, 0, cos_p]
        ])

        # Rx (roll around X-axis)
        Rx = np.array([
            [1, 0, 0],
            [0, cos_r, -sin_r],
            [0, sin_r, cos_r]
        ])

        # Combined rotation: R = Rz * Ry * Rx (intrinsic ZYX)
        R = Rz @ Ry @ Rx

        # Extract basis vectors (rows of rotation matrix)
        forward = R[0]  # First row
        right = R[1]    # Second row
        up = R[2]       # Third row

        return RotMat(forward, right, up)

    def verify_orthogonal(self, tol=1e-6):
        """Verify orthogonality properties

        Valid rotation matrices must satisfy:
        1. All vectors are unit length (magnitude = 1)
        2. All vectors are perpendicular (dot product = 0)
        3. Right-hand rule (forward × right = up)

        Args:
            tol: tolerance for floating point comparison

        Returns:
            bool: True if orthogonal, raises AssertionError otherwise
        """
        # Check unit length
        f_len = np.linalg.norm(self.forward)
        r_len = np.linalg.norm(self.right)
        u_len = np.linalg.norm(self.up)

        assert np.allclose(f_len, 1.0, atol=tol), f"Forward not unit: {f_len}"
        assert np.allclose(r_len, 1.0, atol=tol), f"Right not unit: {r_len}"
        assert np.allclose(u_len, 1.0, atol=tol), f"Up not unit: {u_len}"

        # Check perpendicularity
        fr_dot = np.dot(self.forward, self.right)
        fu_dot = np.dot(self.forward, self.up)
        ru_dot = np.dot(self.right, self.up)

        assert np.allclose(fr_dot, 0.0, atol=tol), f"Forward-Right not perpendicular: {fr_dot}"
        assert np.allclose(fu_dot, 0.0, atol=tol), f"Forward-Up not perpendicular: {fu_dot}"
        assert np.allclose(ru_dot, 0.0, atol=tol), f"Right-Up not perpendicular: {ru_dot}"

        # Check right-hand rule
        cross = np.cross(self.forward, self.right)
        assert np.allclose(cross, self.up, atol=tol), f"Right-hand rule failed: {cross} != {self.up}"

        return True

    def to_dict(self):
        """Export to dictionary for JSON serialization

        Returns:
            dict with forward, right, up vectors as x/y/z components
        """
        return {
            'forward': {
                'x': float(self.forward[0]),
                'y': float(self.forward[1]),
                'z': float(self.forward[2])
            },
            'right': {
                'x': float(self.right[0]),
                'y': float(self.right[1]),
                'z': float(self.right[2])
            },
            'up': {
                'x': float(self.up[0]),
                'y': float(self.up[1]),
                'z': float(self.up[2])
            }
        }

    def __repr__(self):
        return f"RotMat(forward={self.forward}, right={self.right}, up={self.up})"


def generate_rotation_data(rotation_type='yaw', steps=360):
    """Generate rotation matrix data for visualization

    Args:
        rotation_type: Type of rotation to visualize
            - 'yaw': Pure rotation around Z-axis (0 to 360°)
            - 'pitch': Pure rotation around Y-axis (0 to 360°)
            - 'roll': Pure rotation around X-axis (0 to 360°)
            - 'combined': Mixed yaw + 0.5*pitch + 0.3*roll
        steps: Number of angle samples (default 360 = 1° increments)

    Returns:
        List of dicts containing:
            - angle_deg: angle in degrees (0-359)
            - angle_rad: angle in radians (0-2π)
            - rotmat: rotation matrix as dict (forward, right, up components)
    """
    data = []

    for i in range(steps):
        # Convert step to radians (0 to 2π)
        angle_rad = (i / steps) * 2 * np.pi
        angle_deg = i

        # Generate rotation based on type
        if rotation_type == 'yaw':
            # Pure yaw rotation
            rotmat = RotMat.from_euler_angles(angle_rad, 0, 0)
        elif rotation_type == 'pitch':
            # Pure pitch rotation
            rotmat = RotMat.from_euler_angles(0, angle_rad, 0)
        elif rotation_type == 'roll':
            # Pure roll rotation
            rotmat = RotMat.from_euler_angles(0, 0, angle_rad)
        else:  # combined
            # Combined rotation (scaled)
            rotmat = RotMat.from_euler_angles(
                angle_rad,           # Full yaw
                angle_rad * 0.5,     # Half pitch
                angle_rad * 0.3      # 30% roll
            )

        # Verify orthogonality (for debugging)
        try:
            rotmat.verify_orthogonal()
        except AssertionError as e:
            print(f"Warning: Orthogonality check failed at {rotation_type} {angle_deg}°: {e}")

        # Store data point
        data.append({
            'angle_deg': angle_deg,
            'angle_rad': float(angle_rad),
            'angle_rad_str': f'{angle_rad:.4f}',
            'rotmat': rotmat.to_dict(),
        })

    return data


def generate_html_visualization(all_data):
    """Generate complete HTML/JavaScript visualization

    Args:
        all_data: dict with keys 'yaw', 'pitch', 'roll', 'combined'
                 each containing list of rotation data dicts

    Returns:
        str: HTML content with embedded visualization
    """
    # Convert data to JSON string for embedding
    data_json = json.dumps(all_data, indent=2)

    # Load Octane OBJ model
    octane_obj_path = Path("C:/Users/nhafe/RocketSimVis/data/Octane.obj")
    octane_obj_data = ""
    if octane_obj_path.exists():
        with open(octane_obj_path, 'r', encoding='utf-8') as f:
            octane_obj_data = f.read()
            # Escape backticks for JavaScript template string
            octane_obj_data = octane_obj_data.replace('`', '\\`')
    else:
        print(f"Warning: Octane.obj not found at {octane_obj_path}, using procedural model")

    html_template = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>RocketSim Rotation Matrix Visualization</title>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/three.js/r128/three.min.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/three@r128/examples/js/loaders/OBJLoader.js"></script>
    <script src="https://cdn.plot.ly/plotly-latest.min.js"></script>
    <style>
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}

        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Roboto', 'Oxygen', 'Ubuntu', sans-serif;
            background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
            color: #333;
            min-height: 100vh;
            padding: 20px;
        }}

        .container {{
            max-width: 1600px;
            margin: 0 auto;
        }}

        .header {{
            text-align: center;
            color: white;
            margin-bottom: 30px;
        }}

        .header h1 {{
            font-size: 2.5em;
            margin-bottom: 10px;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
        }}

        .header p {{
            font-size: 1.1em;
            opacity: 0.9;
        }}

        .controls {{
            background: white;
            border-radius: 8px;
            padding: 20px;
            margin-bottom: 20px;
            box-shadow: 0 2px 8px rgba(0,0,0,0.1);
        }}

        .control-group {{
            margin-bottom: 15px;
        }}

        .control-group label {{
            display: block;
            font-weight: 600;
            margin-bottom: 8px;
            color: #333;
        }}

        .radio-group {{
            display: flex;
            gap: 20px;
            flex-wrap: wrap;
        }}

        .radio-group input[type="radio"] {{
            margin-right: 5px;
        }}

        .radio-group label {{
            display: inline;
            font-weight: normal;
            margin: 0;
        }}

        .slider-container {{
            display: flex;
            align-items: center;
            gap: 15px;
        }}

        input[type="range"] {{
            flex: 1;
            min-width: 200px;
            height: 6px;
            border-radius: 3px;
            background: linear-gradient(to right, #ff6b6b, #4ecdc4);
            outline: none;
            -webkit-appearance: none;
            appearance: none;
        }}

        input[type="range"]::-webkit-slider-thumb {{
            -webkit-appearance: none;
            appearance: none;
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: white;
            cursor: pointer;
            box-shadow: 0 2px 4px rgba(0,0,0,0.2);
        }}

        input[type="range"]::-moz-range-thumb {{
            width: 20px;
            height: 20px;
            border-radius: 50%;
            background: white;
            cursor: pointer;
            box-shadow: 0 2px 4px rgba(0,0,0,0.2);
            border: none;
        }}

        .angle-display {{
            min-width: 120px;
            padding: 8px 12px;
            background: #f0f0f0;
            border-radius: 4px;
            font-family: 'Courier New', monospace;
            font-weight: 600;
        }}

        button {{
            padding: 8px 16px;
            background: #4ecdc4;
            color: white;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-weight: 600;
            transition: background 0.3s ease;
        }}

        button:hover {{
            background: #2a9d8f;
        }}

        button:active {{
            transform: scale(0.98);
        }}

        .main-content {{
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
            margin-bottom: 20px;
        }}

        .panel {{
            background: white;
            border-radius: 8px;
            box-shadow: 0 2px 8px rgba(0,0,0,0.1);
            overflow: hidden;
        }}

        .panel-header {{
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 15px;
            font-weight: 600;
            font-size: 1.1em;
        }}

        .panel-content {{
            padding: 15px;
            min-height: 500px;
            display: flex;
            flex-direction: column;
        }}

        #canvas3d {{
            width: 100%;
            height: 500px;
            background: linear-gradient(135deg, #f5f7fa 0%, #c3cfe2 100%);
        }}

        .plots-container {{
            display: flex;
            flex-direction: column;
            gap: 10px;
        }}

        .plot-frame {{
            flex: 1;
            min-height: 150px;
        }}

        .legend {{
            background: white;
            border-radius: 8px;
            padding: 15px;
            margin-bottom: 20px;
            box-shadow: 0 2px 8px rgba(0,0,0,0.1);
        }}

        .legend h3 {{
            margin-bottom: 10px;
            color: #333;
        }}

        .legend-item {{
            display: flex;
            align-items: center;
            margin: 8px 0;
            padding: 8px;
            background: #f9f9f9;
            border-radius: 4px;
        }}

        .legend-color {{
            width: 20px;
            height: 20px;
            border-radius: 3px;
            margin-right: 10px;
        }}

        .vector-values {{
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 10px;
            margin-top: 15px;
        }}

        .vector-box {{
            background: #f0f0f0;
            padding: 10px;
            border-radius: 4px;
            font-size: 0.9em;
            font-family: 'Courier New', monospace;
        }}

        .vector-box strong {{
            display: block;
            margin-bottom: 5px;
            color: #333;
        }}

        .coordinate-system {{
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 8px;
            margin-bottom: 10px;
        }}

        .axis-info {{
            background: #f0f0f0;
            padding: 8px;
            border-left: 4px solid #667eea;
            border-radius: 4px;
            font-size: 0.85em;
        }}

        .axis-info.x {{
            border-left-color: #ff6b6b;
        }}

        .axis-info.y {{
            border-left-color: #51cf66;
        }}

        .axis-info.z {{
            border-left-color: #339af0;
        }}

        @media (max-width: 1200px) {{
            .main-content {{
                grid-template-columns: 1fr;
            }}
        }}

        @media (max-width: 600px) {{
            .slider-container {{
                flex-direction: column;
            }}

            .header h1 {{
                font-size: 1.8em;
            }}

            .radio-group {{
                flex-direction: column;
                gap: 10px;
            }}
        }}
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🎯 RocketSim Rotation Matrix Visualization</h1>
            <p>Understand how forward, right, and up vectors change with rotation</p>
        </div>

        <div class="controls">
            <div class="control-group">
                <label>Rotation Type:</label>
                <div class="radio-group">
                    <div>
                        <input type="radio" id="type-yaw" name="rotation-type" value="yaw" checked>
                        <label for="type-yaw">Yaw (Z-axis)</label>
                    </div>
                    <div>
                        <input type="radio" id="type-pitch" name="rotation-type" value="pitch">
                        <label for="type-pitch">Pitch (Y-axis)</label>
                    </div>
                    <div>
                        <input type="radio" id="type-roll" name="rotation-type" value="roll">
                        <label for="type-roll">Roll (X-axis)</label>
                    </div>
                    <div>
                        <input type="radio" id="type-combined" name="rotation-type" value="combined">
                        <label for="type-combined">Combined</label>
                    </div>
                </div>
            </div>

            <div class="control-group">
                <label>Angle:</label>
                <div class="slider-container">
                    <input type="range" id="angle-slider" min="0" max="359" value="0">
                    <div class="angle-display" id="angle-display">0° (0.0000 rad)</div>
                    <button id="play-button">▶ Play</button>
                </div>
            </div>

            <div class="control-group">
                <label>Animation Speed:</label>
                <div class="slider-container">
                    <input type="range" id="speed-slider" min="0.1" max="2" step="0.1" value="1">
                    <span id="speed-display">1.0x</span>
                </div>
            </div>
        </div>

        <div class="legend">
            <h3>Coordinate System Legend</h3>
            <div class="coordinate-system">
                <div class="axis-info x">
                    <strong>X-axis:</strong>
                    Left (-) / Right (+)
                </div>
                <div class="axis-info y">
                    <strong>Y-axis:</strong>
                    Blue goal (-) / Orange goal (+)
                </div>
                <div class="axis-info z">
                    <strong>Z-axis:</strong>
                    Down (-) / Up (+)
                </div>
            </div>

            <h3>Vector Legend</h3>
            <div>
                <div class="legend-item">
                    <div class="legend-color" style="background: #ff4444;"></div>
                    <strong>Forward:</strong> Direction car is facing
                </div>
                <div class="legend-item">
                    <div class="legend-color" style="background: #44ff44;"></div>
                    <strong>Right:</strong> Car's rightward direction
                </div>
                <div class="legend-item">
                    <div class="legend-color" style="background: #4444ff;"></div>
                    <strong>Up:</strong> Car's upward direction
                </div>
            </div>

            <div class="vector-values" id="vector-values">
                <!-- Populated by JavaScript -->
            </div>
        </div>

        <div class="main-content">
            <div class="panel">
                <div class="panel-header">3D Vector Visualization</div>
                <div class="panel-content">
                    <div id="canvas3d"></div>
                </div>
            </div>

            <div class="panel">
                <div class="panel-header">Vector Component Values</div>
                <div class="panel-content">
                    <div class="plots-container">
                        <div class="plot-frame" id="plot-forward"></div>
                        <div class="plot-frame" id="plot-right"></div>
                        <div class="plot-frame" id="plot-up"></div>
                    </div>
                </div>
            </div>
        </div>
    </div>

    <script>
        // Embedded rotation data
        const rotationData = {data_json};

        // Embedded Octane OBJ model
        const octaneObjData = `{octane_obj_data}`;

        // Global state
        let currentAngle = 0;
        let currentType = 'yaw';
        let isPlaying = false;
        let animationSpeed = 1.0;
        let scene, camera, renderer, arrows = {{}}, carGroup;

        // Initialize Three.js scene
        function init3D() {{
            const container = document.getElementById('canvas3d');
            const width = container.clientWidth;
            const height = container.clientHeight;

            scene = new THREE.Scene();
            scene.background = new THREE.Color(0x1a1a2e);  // Dark background for better visibility

            camera = new THREE.PerspectiveCamera(75, width / height, 0.1, 1000);
            camera.position.set(2, 2, 2);
            camera.lookAt(0, 0, 0);

            renderer = new THREE.WebGLRenderer({{ antialias: true }});
            renderer.setSize(width, height);
            renderer.setPixelRatio(window.devicePixelRatio);
            container.appendChild(renderer.domElement);

            // Add grid
            const gridHelper = new THREE.GridHelper(5, 10, 0xcccccc, 0xeeeeee);
            scene.add(gridHelper);

            // Add coordinate axes (thicker for clarity)
            const axisLength = 1.5;
            const axisGeometry = new THREE.BufferGeometry();

            // X-axis (red)
            const xLine = new THREE.Line(
                new THREE.BufferGeometry().setFromPoints([
                    new THREE.Vector3(-axisLength, 0, 0),
                    new THREE.Vector3(axisLength, 0, 0)
                ]),
                new THREE.LineBasicMaterial({{ color: 0xaa4444, linewidth: 2 }})
            );
            scene.add(xLine);

            // Y-axis (green)
            const yLine = new THREE.Line(
                new THREE.BufferGeometry().setFromPoints([
                    new THREE.Vector3(0, -axisLength, 0),
                    new THREE.Vector3(0, axisLength, 0)
                ]),
                new THREE.LineBasicMaterial({{ color: 0x44aa44, linewidth: 2 }})
            );
            scene.add(yLine);

            // Z-axis (blue)
            const zLine = new THREE.Line(
                new THREE.BufferGeometry().setFromPoints([
                    new THREE.Vector3(0, 0, -axisLength),
                    new THREE.Vector3(0, 0, axisLength)
                ]),
                new THREE.LineBasicMaterial({{ color: 0x4444aa, linewidth: 2 }})
            );
            scene.add(zLine);

            // Origin sphere
            const sphereGeometry = new THREE.SphereGeometry(0.05, 8, 8);
            const sphereMaterial = new THREE.MeshBasicMaterial({{ color: 0x333333 }});
            const sphere = new THREE.Mesh(sphereGeometry, sphereMaterial);
            scene.add(sphere);

            // Create arrow helpers (will be updated)
            createArrows();

            // Create Octane car model
            createOctaneCar();

            // Handle resize
            window.addEventListener('resize', () => {{
                const newWidth = container.clientWidth;
                const newHeight = container.clientHeight;
                camera.aspect = newWidth / newHeight;
                camera.updateProjectionMatrix();
                renderer.setSize(newWidth, newHeight);
            }});

            // Enable mouse controls
            enableMouseControls();

            // Animation loop
            animate();
        }}

        function createArrows() {{
            const arrowLength = 1.2;
            const arrowColors = {{
                forward: 0xff6b6b,
                right: 0x51cf66,
                up: 0x339af0
            }};

            for (let [name, color] of Object.entries(arrowColors)) {{
                const arrowHelper = new THREE.ArrowHelper(
                    new THREE.Vector3(0, 0, 1),
                    new THREE.Vector3(0, 0, 0),
                    arrowLength,
                    color,
                    0.3,
                    0.2
                );
                scene.add(arrowHelper);
                arrows[name] = arrowHelper;
            }}
        }}

        function createOctaneCar() {{
            carGroup = new THREE.Group();
            carGroup.position.set(0, 0, 0);

            // Material definitions
            const bodyMaterial = new THREE.MeshPhongMaterial({{ color: 0xff6600, shininess: 100 }});
            const windowMaterial = new THREE.MeshPhongMaterial({{ color: 0x4488ff, transparent: true, opacity: 0.3 }});
            const wheelMaterial = new THREE.MeshPhongMaterial({{ color: 0x1a1a1a, shininess: 20 }});
            const metalMaterial = new THREE.MeshPhongMaterial({{ color: 0x888888, shininess: 200 }});

            // Main chassis body
            const bodyGeom = new THREE.BoxGeometry(0.4, 0.25, 1.2);
            const body = new THREE.Mesh(bodyGeom, bodyMaterial);
            body.position.z = 0.05;
            body.castShadow = true;
            body.receiveShadow = true;
            carGroup.add(body);

            // Roof
            const roofGeom = new THREE.BoxGeometry(0.35, 0.12, 0.6);
            const roof = new THREE.Mesh(roofGeom, bodyMaterial);
            roof.position.set(0, 0.15, 0.1);
            roof.castShadow = true;
            roof.receiveShadow = true;
            carGroup.add(roof);

            // Front hood section
            const hoodGeom = new THREE.BoxGeometry(0.38, 0.18, 0.4);
            const hood = new THREE.Mesh(hoodGeom, bodyMaterial);
            hood.position.z = 0.55;
            hood.position.y = 0.04;
            hood.castShadow = true;
            hood.receiveShadow = true;
            carGroup.add(hood);

            // Windshield
            const windshieldGeom = new THREE.BoxGeometry(0.32, 0.12, 0.25);
            const windshield = new THREE.Mesh(windshieldGeom, windowMaterial);
            windshield.position.z = 0.4;
            windshield.position.y = 0.1;
            windshield.castShadow = true;
            carGroup.add(windshield);

            // Back bumper
            const bumperGeom = new THREE.BoxGeometry(0.42, 0.1, 0.15);
            const bumper = new THREE.Mesh(bumperGeom, metalMaterial);
            bumper.position.z = -0.55;
            bumper.castShadow = true;
            carGroup.add(bumper);

            // Wheels
            const wheelGeom = new THREE.CylinderGeometry(0.12, 0.12, 0.1, 16);
            const positions = [
                {{ x: -0.22, y: -0.15, z: 0.3 }},   // Front left
                {{ x: 0.22, y: -0.15, z: 0.3 }},    // Front right
                {{ x: -0.22, y: -0.15, z: -0.3 }},  // Rear left
                {{ x: 0.22, y: -0.15, z: -0.3 }}    // Rear right
            ];

            positions.forEach(pos => {{
                const wheel = new THREE.Mesh(wheelGeom, wheelMaterial);
                wheel.rotation.z = Math.PI / 2;
                wheel.position.set(pos.x, pos.y, pos.z);
                wheel.castShadow = true;
                wheel.receiveShadow = true;
                carGroup.add(wheel);
            }});

            // Tail lights
            const lightGeom = new THREE.BoxGeometry(0.1, 0.08, 0.05);
            const lightMaterial = new THREE.MeshPhongMaterial({{ color: 0xff3333, emissive: 0xff0000 }});
            [[-0.18, -0.08, -0.6], [0.18, -0.08, -0.6]].forEach(pos => {{
                const light = new THREE.Mesh(lightGeom, lightMaterial);
                light.position.set(...pos);
                light.castShadow = true;
                carGroup.add(light);
            }});

            // Add comprehensive lighting
            const ambientLight = new THREE.AmbientLight(0xffffff, 0.7);
            scene.add(ambientLight);

            const directionalLight = new THREE.DirectionalLight(0xffffff, 1.2);
            directionalLight.position.set(3, 4, 3);
            directionalLight.castShadow = true;
            scene.add(directionalLight);

            const pointLight = new THREE.PointLight(0xffffff, 0.8, 30);
            pointLight.position.set(-3, 2, -3);
            scene.add(pointLight);

            scene.add(carGroup);

            // Set camera position
            camera.position.set(2.5, 1.8, 2.5);
            camera.lookAt(carGroup.position);
            camera.updateProjectionMatrix();
        }}

        function updateCarRotation(data) {{
            if (!carGroup) return;

            const f = data.rotmat.forward;
            const r = data.rotmat.right;
            const u = data.rotmat.up;

            // Convert RocketSim coordinates to Three.js coordinates
            // RocketSim: X=right, Y=forward(orange goal), Z=up
            // Three.js: X=right, Y=up, Z=backward
            // So: RS_X → JS_X, RS_Y → JS_-Z, RS_Z → JS_Y

            const f_js = {{ x: f.x, y: f.z, z: -f.y }};
            const r_js = {{ x: r.x, y: r.z, z: -r.y }};
            const u_js = {{ x: u.x, y: u.z, z: -u.y }};

            // Create rotation matrix from coordinate system basis vectors
            // The three vectors represent where the car's axes point in world space
            // We use a quaternion approach for more reliable rotation

            // Create vectors for the car's local coordinate system
            // Note: The RocketSim vectors are permuted relative to their labels
            // "forward" vector actually represents right direction
            // "right" vector actually represents up direction
            // "up" vector actually represents forward direction
            const axis_x = new THREE.Vector3(f_js.x, f_js.y, f_js.z);  // right (from "forward" vector)
            const axis_y = new THREE.Vector3(r_js.x, r_js.y, r_js.z);  // up (from "right" vector)
            const axis_z = new THREE.Vector3(-u_js.x, -u_js.y, -u_js.z); // forward in Three.js (from "up" vector, negated)

            // Build rotation matrix with these as basis vectors
            const matrix = new THREE.Matrix4();
            matrix.set(
                axis_x.x, axis_y.x, axis_z.x, 0,
                axis_x.y, axis_y.y, axis_z.y, 0,
                axis_x.z, axis_y.z, axis_z.z, 0,
                0, 0, 0, 1
            );

            // Normalize the matrix to ensure it's orthonormal
            const m = matrix.elements;
            const scale = Math.sqrt(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
            if (scale > 0) {{
                const invScale = 1.0 / scale;
                for (let i = 0; i < 12; i++) m[i] *= invScale;
            }}

            carGroup.setRotationFromMatrix(matrix);
        }}

        function updateArrows(data) {{
            const f = data.rotmat.forward;
            const r = data.rotmat.right;
            const u = data.rotmat.up;

            arrows.forward.setDirection(new THREE.Vector3(f.x, f.y, f.z));
            arrows.right.setDirection(new THREE.Vector3(r.x, r.y, r.z));
            arrows.up.setDirection(new THREE.Vector3(u.x, u.y, u.z));
        }}

        function enableMouseControls() {{
            let isDragging = false;
            let previousMousePosition = {{ x: 0, y: 0 }};

            const canvas = renderer.domElement;

            canvas.addEventListener('mousedown', (e) => {{
                isDragging = true;
                previousMousePosition = {{ x: e.clientX, y: e.clientY }};
            }});

            canvas.addEventListener('mousemove', (e) => {{
                if (isDragging) {{
                    const deltaX = e.clientX - previousMousePosition.x;
                    const deltaY = e.clientY - previousMousePosition.y;

                    // Rotate camera around origin
                    const radius = camera.position.length();
                    let theta = Math.atan2(camera.position.x, camera.position.z);
                    let phi = Math.acos(camera.position.y / radius);

                    theta -= (deltaX * 0.01);
                    phi += (deltaY * 0.01);

                    phi = Math.max(0.1, Math.min(Math.PI - 0.1, phi));

                    camera.position.x = radius * Math.sin(phi) * Math.sin(theta);
                    camera.position.y = radius * Math.cos(phi);
                    camera.position.z = radius * Math.sin(phi) * Math.cos(theta);
                    camera.lookAt(0, 0, 0);

                    previousMousePosition = {{ x: e.clientX, y: e.clientY }};
                }}
            }});

            canvas.addEventListener('mouseup', () => {{
                isDragging = false;
            }});

            canvas.addEventListener('wheel', (e) => {{
                e.preventDefault();
                const radius = camera.position.length();
                const newRadius = radius + (e.deltaY > 0 ? 0.5 : -0.5);
                const direction = camera.position.normalize();
                camera.position.copy(direction.multiplyScalar(Math.max(0.5, Math.min(10, newRadius))));
            }}, {{ passive: false }});
        }}

        function animate() {{
            requestAnimationFrame(animate);

            if (isPlaying) {{
                currentAngle = (currentAngle + 0.5 * animationSpeed) % 360;
                document.getElementById('angle-slider').value = Math.floor(currentAngle);
                updateVisualization();
            }}

            renderer.render(scene, camera);
        }}

        function updateVisualization() {{
            const data = rotationData[currentType][currentAngle];

            // Update 3D arrows
            updateArrows(data);

            // Update car rotation
            updateCarRotation(data);

            // Update angle display
            const angleRad = data.angle_rad;
            document.getElementById('angle-display').textContent =
                `${{currentAngle}}° (${{angleRad.toFixed(4)}} rad)`;

            // Update vector values
            updateVectorDisplay(data);

            // Update plots
            updatePlots(data);
        }}

        function updateVectorDisplay(data) {{
            const f = data.rotmat.forward;
            const r = data.rotmat.right;
            const u = data.rotmat.up;

            const valuesHtml = `
                <div class="vector-box">
                    <strong>Forward →</strong>
                    x: ${{f.x.toFixed(3)}}<br>
                    y: ${{f.y.toFixed(3)}}<br>
                    z: ${{f.z.toFixed(3)}}
                </div>
                <div class="vector-box">
                    <strong>Right →</strong>
                    x: ${{r.x.toFixed(3)}}<br>
                    y: ${{r.y.toFixed(3)}}<br>
                    z: ${{r.z.toFixed(3)}}
                </div>
                <div class="vector-box">
                    <strong>Up ↑</strong>
                    x: ${{u.x.toFixed(3)}}<br>
                    y: ${{u.y.toFixed(3)}}<br>
                    z: ${{u.z.toFixed(3)}}
                </div>
            `;
            document.getElementById('vector-values').innerHTML = valuesHtml;
        }}

        function updatePlots(currentData) {{
            // Extract all component values up to current angle
            const fData = {{}}, rData = {{}}, uData = {{}};

            for (let i = 0; i <= currentAngle; i++) {{
                const d = rotationData[currentType][i];
                const angle = d.angle_deg;

                fData[angle] = d.rotmat.forward;
                rData[angle] = d.rotmat.right;
                uData[angle] = d.rotmat.up;
            }}

            // Create plot data
            const createTraces = (data, color) => {{
                const angles = Object.keys(data).map(Number).sort((a,b) => a-b);
                return [
                    {{x: angles, y: angles.map(a => data[a].x), name: 'X', line: {{color: '#ff6b6b'}}}},
                    {{x: angles, y: angles.map(a => data[a].y), name: 'Y', line: {{color: '#51cf66'}}}},
                    {{x: angles, y: angles.map(a => data[a].z), name: 'Z', line: {{color: '#339af0'}}}}
                ];
            }};

            const layout = {{
                height: 140,
                margin: {{ t: 20, b: 30, l: 40, r: 20 }},
                showlegend: true,
                legend: {{ orientation: 'h', x: 0, y: 1.1 }},
                xaxis: {{ title: 'Angle (°)' }},
                yaxis: {{ range: [-1.2, 1.2] }},
                hovermode: 'closest'
            }};

            Plotly.react('plot-forward', createTraces(fData), {{...layout, title: 'Forward Vector'}}, {{responsive: true}});
            Plotly.react('plot-right', createTraces(rData), {{...layout, title: 'Right Vector'}}, {{responsive: true}});
            Plotly.react('plot-up', createTraces(uData), {{...layout, title: 'Up Vector'}}, {{responsive: true}});
        }}

        // Event listeners
        document.getElementById('angle-slider').addEventListener('input', (e) => {{
            currentAngle = parseInt(e.target.value);
            updateVisualization();
        }});

        document.querySelectorAll('input[name="rotation-type"]').forEach(radio => {{
            radio.addEventListener('change', (e) => {{
                currentType = e.target.value;
                currentAngle = 0;
                document.getElementById('angle-slider').value = 0;
                updateVisualization();
            }});
        }});

        document.getElementById('play-button').addEventListener('click', () => {{
            isPlaying = !isPlaying;
            document.getElementById('play-button').textContent = isPlaying ? '⏸ Pause' : '▶ Play';
        }});

        document.getElementById('speed-slider').addEventListener('input', (e) => {{
            animationSpeed = parseFloat(e.target.value);
            document.getElementById('speed-display').textContent = animationSpeed.toFixed(1) + 'x';
        }});

        // Initialize on load
        window.addEventListener('load', () => {{
            init3D();
            updateVisualization();
        }});
    </script>
</body>
</html>
"""
    return html_template


def run_tests():
    """Run validation tests on RotMat class"""
    print("[TEST] Running rotation matrix tests...")

    # Test 1: Known angles (yaw)
    print("\n  Test 1: Pure Yaw Rotations")
    test_cases = [
        (0, "0 deg", [1, 0, 0]),           # Initial forward direction
        (np.pi/2, "90 deg", [0, -1, 0]),   # After 90 degree yaw
        (np.pi, "180 deg", [-1, 0, 0]),    # After 180 degree yaw
        (3*np.pi/2, "270 deg", [0, 1, 0]) # After 270 degree yaw
    ]

    for angle_rad, angle_str, expected_forward in test_cases:
        rotmat = RotMat.from_euler_angles(angle_rad, 0, 0)
        forward = rotmat.forward
        matches = np.allclose(forward, expected_forward, atol=1e-6)
        status = "[OK]" if matches else "[FAIL]"
        print(f"    {status} Yaw {angle_str}: forward = [{forward[0]:.3f}, {forward[1]:.3f}, {forward[2]:.3f}] (expected {expected_forward})")

    # Test 2: Orthogonality
    print("\n  Test 2: Orthogonality Properties")
    for angle in [0, np.pi/4, np.pi/2, np.pi]:
        rotmat = RotMat.from_euler_angles(angle, angle/2, angle/3)
        try:
            rotmat.verify_orthogonal()
            print(f"    [OK] Angle {np.degrees(angle):.0f} deg: Orthogonal")
        except AssertionError as e:
            print(f"    [FAIL] Angle {np.degrees(angle):.0f} deg: {e}")

    print("\n[OK] Tests complete!\n")


def main():
    """Main function: generate visualization"""
    print("\n" + "="*60)
    print("  RocketSim Rotation Matrix Visualization Tool")
    print("="*60 + "\n")

    # Run validation tests
    run_tests()

    # Generate rotation data for all types
    print("[DATA] Generating rotation data...")
    rotation_types = ['yaw', 'pitch', 'roll', 'combined']
    all_data = {}

    for rot_type in rotation_types:
        print(f"  - {rot_type.capitalize()} rotation...", end="", flush=True)
        all_data[rot_type] = generate_rotation_data(rot_type, steps=360)
        print(" [OK]")

    # Generate HTML visualization
    print("\n[HTML] Generating HTML visualization...")
    html_content = generate_html_visualization(all_data)

    # Write output file
    output_path = Path('rotation_viewer.html')
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(html_content)

    print(f"\n[OK] Success! Generated: {output_path.absolute()}")
    print(f"\n[INFO] Open this file in your web browser to view the interactive visualization:")
    print(f"   • 3D vector arrows showing forward, right, and up vectors")
    print(f"   • Component plots showing x, y, z values vs. rotation angle")
    print(f"   • Interactive controls: slider, play/pause, speed control")
    print(f"   • Support for pure yaw, pitch, roll, and combined rotations")
    print(f"\nCoordinate System (RocketSim Convention):")
    print(f"  X-axis: Left (-) / Right (+)")
    print(f"  Y-axis: Blue goal (-) / Orange goal (+)")
    print(f"  Z-axis: Down (-) / Up (+)\n")


if __name__ == '__main__':
    main()
