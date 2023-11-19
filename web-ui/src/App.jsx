import { useState, useEffect } from 'react'
import {
  Button,
  ButtonGroup,
  TextField,
  InputLabel,
  IconButton,
  Select,
  MenuItem,
} from '@mui/material';
import './styles/App.css'

import RefreshIcon from '@mui/icons-material/Refresh';
import ArrowDropUpIcon from '@mui/icons-material/ArrowDropUp';
import ArrowDropDownIcon from '@mui/icons-material/ArrowDropDown';

// self/delay: wait time between push button and start
// long/exposure: how long exposure
// interval: time between pictures
// frames: number of frames
// hold: on/off

const backendUrl = "http://192.168.1.73:8000"

const defaultState = {
  delay: 0,
  exposure: 0,
  interval: 0,
  frames: 0,
  hold: false,
  camera: undefined,
  connected: false,
  shooting: false,
};

function App() {
  const [ state, setState ] = useState(defaultState);
  const [ cameras, setCameras ] = useState([]);

  const onChangeState = (value, field) => {
    setState(prev => {
      return {...prev, [field]: value};
    });
  };

  const onChangeState2 = (s) => {
    setState(prev => {
      return {...prev, ...s};
    });
  };

  const refreshCameras = async (event) => {
    event.preventDefault();

    const response = await fetch(
      `${backendUrl}/api/cameras`,
      { method: "GET" }
    );

    const json = await response.json();
    console.log(json);

    setCameras(json);

    if (json && json.length > 0) {
      onChangeState(json[0].id, 'camera')
    }
  };

  const connectCamera = async (event) => {
    event.preventDefault();

    const response = await fetch(
      `${backendUrl}/api/camera/${state.camera}/connect`,
      { method: "POST" }
    );

    const json = await response.json();
    console.log(json);

    switch (json.status) {
      case 'success':
        const state = json.state;
        onChangeState2({ ...state, connected: true });
        break;

      case 'failure':
        alert(json.description);
        break;
    }
  };

  const disconnectCamera = async (event) => {
    event.preventDefault();

    const response = await fetch(
      `${backendUrl}/api/camera/${state.camera}/disconnect`,
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
      `${backendUrl}/api/camera/${state.camera}/start-shoot`,
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
      `${backendUrl}/api/camera/${state.camera}/state`,
      { method: "GET" }
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
      const timer = setInterval(poolShooting, 1000);
      return () => clearInterval(timer);
    }
  }, [state.shooting]);

  const stopShooting = async (event) => {
    event.preventDefault();

    const response = await fetch(
      `${backendUrl}/api/camera/${state.camera}/stop-shoot`,
      { method: "POST" }
    );

    const json = await response.json();
    console.log(json);

    if (json.status === 'success')
      onChangeState(false, 'shooting');
  };

  const camerasOptions = cameras.map(camera => {
    return (
      <MenuItem key={`camera-${camera.id}`} value={camera.id}>
        {camera.description}
      </MenuItem>
    );
  });

  return (
    <div style={{ display: 'flex', flexDirection: 'column', rowGap: '30px' }}>
      <div>
        <InputLabel id="camera-select-label">Camera</InputLabel>
        <Select
          labelId="camera-select-label"
          value={state.camera ?? ''}
          label="Camera"
          disabled={state.connected}
          onChange={ (e) => {
            console.log(e)
            onChangeState(e.target.value, 'camera')
          }}
        >
          {camerasOptions}
        </Select>

        <IconButton
          disabled={state.connected}
          onClick={refreshCameras}
        >
          <RefreshIcon />
        </IconButton>

        {state.camera && !state.connected && (
          <Button
            onClick={connectCamera}
          >
            Connect
          </Button>
        )}

        {state.camera && state.connected && (
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
          onChange={ (e) => onChangeState(parseInt(e.target.value), 'delay') }
        />

        <TextField
          label="Exposure (seconds)"
          type="number"
          variant="outlined"
          InputLabelProps={{ shrink: true }}
          inputProps={{ inputMode: 'numeric' }}
          disabled={!state.connected || state.shooting}
          value={state.exposure}
          onChange={ (e) => onChangeState(parseInt(e.target.value), 'exposure') }
        />

        <TextField
          label="Interval (seconds)"
          type="number"
          variant="outlined"
          InputLabelProps={{ shrink: true }}
          inputProps={{ inputMode: 'numeric' }}
          disabled={!state.connected || state.shooting}
          value={state.interval}
          onChange={ (e) => onChangeState(parseInt(e.target.value), 'interval') }
        />

        <TextField
          label="Frames"
          type="number"
          variant="outlined"
          InputLabelProps={{ shrink: true }}
          inputProps={{ inputMode: 'numeric' }}
          disabled={!state.connected || state.shooting}
          value={state.frames}
          onChange={ (e) => onChangeState(parseInt(e.target.value), 'frames') }
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
      </div>
    </div>
  )
}

export default App
