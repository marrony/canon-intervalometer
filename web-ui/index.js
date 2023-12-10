htmx.config.defaultSettleDelay = 1000;
htmx.config.defaultSwapDelay = 1000;

htmx.on("htmx:after-swap", function(evt) {
  if (evt.target && evt.target.nodeName === "INPUT")
    evt.target.focus();
});

htmx.on("htmx:responseError", function(evt) {
  console.log(evt);
});
