import { useState } from 'react'
import './styles/App.css'

function App() {
  const callBackend = async (event) => {
    const response = await fetch(
      'http://localhost:8000/api/hello',
      { method: "GET" }
    );

    console.log(await response.json());
  };

  return (
    <>
      <h1>Hello world</h1>
      <button onClick={callBackend}>Click me</button>
    </>
  )
}

export default App
