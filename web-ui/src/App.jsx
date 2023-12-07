import { useRef, useState, useEffect } from 'react'
import {
  Button,
  TextField,
  IconButton,
} from '@mui/material';
import './styles/App.css'

import RefreshIcon from '@mui/icons-material/Refresh';

// self/delay: wait time between push button and start
// long/exposure: how long exposure
// interval: time between pictures
// frames: number of frames
// hold: on/off

const backendUrl = (() => {
  const url = import.meta.env.VITE_BACKEND_URL;

  if (url && url !== "") return url;

  return location.origin;
})();

const defaultState = {
  delay: 0,
  exposure: 0,
  interval: 0,
  frames: 0,
  hold: false,
  connected: false,
  shooting: false,
};

const defaultCamera = {
  available: false,
  description: 'No camera detected'
};

// workaround to avoid https://react.dev/blog/2022/03/29/react-v18#new-strict-mode-behaviors
function useOnMountUnsafe(effect) {
  const initialized = useRef(false)

  useEffect(() => {
    if (!initialized.current) {
      initialized.current = true
      effect()
    }
  }, []);
}

function App() {
  const [state, setState] = useState(defaultState);
  const [camera, setCamera] = useState(defaultCamera);

  useOnMountUnsafe(() => refreshCameras());

  const onChangeState = (value, field) => {
    setState(prev => {
      return { ...prev, [field]: value };
    });
  };

  const refreshCameras = async () => {
    const response = await fetch(
      `${backendUrl}/api/camera`,
      { method: "GET" }
    );

    const json = await response.json();
    console.log(json);

    if (json.status === 'success') {
      setCamera({
        available: true,
        description: json.camera.description
      });
      setState(json.state);
    } else {
      setCamera(defaultCamera);
      setState(defaultState);
    }
  };

  const connectCamera = async () => {
    const response = await fetch(
      `${backendUrl}/api/camera/connect`,
      { method: "POST" }
    );

    const json = await response.json();
    console.log(json);

    switch (json.status) {
      case 'success':
        setState(json.state);
        break;

      case 'failure':
        alert(json.description);
        break;
    }
  };

  const disconnectCamera = async (event) => {
    event.preventDefault();

    const response = await fetch(
      `${backendUrl}/api/camera/disconnect`,
      { method: "POST" }
    );

    const json = await response.json();
    console.log(json);

    if (json.status === 'success')
      onChangeState(false, 'connected');
  }

  const startShooting = async (event) => {
    event.preventDefault();

    const response = await fetch(
      `${backendUrl}/api/camera/start-shoot`,
      {
        method: "POST",
        body: JSON.stringify({
          delay: state.delay,
          exposure: state.exposure,
          interval: state.interval,
          frames: state.frames,
        })
      }
    );

    const json = await response.json();
    console.log(json);

    if (json.status === 'success') {
      onChangeState(true, 'shooting');
    }
  };

  const getState = async () => {
    const response = await fetch(
      `${backendUrl}/api/camera/state`,
      { method: "POST" }
    );

    return await response.json();
  };

  const poolShooting = async () => {
    const json = await getState();
    console.log(json);

    if (
      json.status === 'success' &&
      json.state.shooting !== state.shooting
    ) {
      onChangeState(json.shooting, 'shooting');
    }
  };

  useEffect(() => {
    if (state.shooting) {
      const poolingTime = import.meta.env.VITE_POOLING_TIME ?? 1000;
      const timer = setInterval(poolShooting, poolingTime);
      return () => clearInterval(timer);
    }
  }, [state.shooting]);

  const stopShooting = async (event) => {
    event.preventDefault();

    const response = await fetch(
      `${backendUrl}/api/camera/stop-shoot`,
      { method: "POST" }
    );

    const json = await response.json();
    console.log(json);

    if (json.status === 'success')
      onChangeState(false, 'shooting');
  };

  const takePicture = async (event) => {
    event.preventDefault();

    const response = await fetch(
      `${backendUrl}/api/camera/take-picture`,
      { method: "POST" }
    );

    const json = await response.json();
    console.log(json);
  };

  return (
    <div style={{ display: 'flex', flexDirection: 'column', rowGap: '30px' }}>
      <div>
        <TextField
          label="Camera"
          variant="outlined"
          InputLabelProps={{ shrink: true }}
          disabled={true}
          value={camera.description}
        />

        <IconButton
          disabled={state.connected}
          onClick={refreshCameras}
        >
          <RefreshIcon />
        </IconButton>

        {camera.available && !state.connected && (
          <Button
            onClick={connectCamera}
          >
            Connect
          </Button>
        )}

        {camera.available && state.connected && (
          <Button
            disabled={state.shooting}
            onClick={disconnectCamera}
          >
            Disconnect
          </Button>
        )}
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', rowGap: '30px' }}>
        <TextField
          label="Delay (seconds)"
          type="number"
          variant="outlined"
          InputLabelProps={{ shrink: true }}
          inputProps={{ inputMode: 'numeric' }}
          disabled={!state.connected || state.shooting}
          value={state.delay}
          onChange={(e) => onChangeState(parseInt(e.target.value), 'delay')}
        />

        <TextField
          label="Exposure (seconds)"
          type="number"
          variant="outlined"
          InputLabelProps={{ shrink: true }}
          inputProps={{ inputMode: 'numeric' }}
          disabled={!state.connected || state.shooting}
          value={state.exposure}
          onChange={(e) => onChangeState(parseInt(e.target.value), 'exposure')}
        />

        <TextField
          label="Interval (seconds)"
          type="number"
          variant="outlined"
          InputLabelProps={{ shrink: true }}
          inputProps={{ inputMode: 'numeric' }}
          disabled={!state.connected || state.shooting}
          value={state.interval}
          onChange={(e) => onChangeState(parseInt(e.target.value), 'interval')}
        />

        <TextField
          label="Frames"
          type="number"
          variant="outlined"
          InputLabelProps={{ shrink: true }}
          inputProps={{ inputMode: 'numeric' }}
          disabled={!state.connected || state.shooting}
          value={state.frames}
          onChange={(e) => onChangeState(parseInt(e.target.value), 'frames')}
        />
      </div>

      <div style={{ display: 'flex', flexDirection: 'row', columnGap: '10px', justifyContent: 'center' }}>
        {!state.shooting && (
          <Button
            disabled={!state.connected}
            variant="contained"
            onClick={startShooting}
          >Start</Button>
        )}

        {state.shooting && (
          <Button
            disabled={!state.connected}
            variant="contained"
            onClick={stopShooting}
          >Stop</Button>
        )}

        <Button
          disabled={!state.connected}
          variant="contained"
          onClick={takePicture}
        >Take Picture</Button>
      </div>
    </div>
  )
}

export default App
