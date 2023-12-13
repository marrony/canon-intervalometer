// htmx.config.defaultSettleDelay = 0;
// htmx.config.defaultSwapDelay = 0;

htmx.on("htmx:afterSwap", function(evt) {
  // if (evt.target && evt.target.nodeName === "INPUT")
  //   evt.target.focus();
});

htmx.on("htmx:responseError", function(evt) {
  console.log(evt);
});
