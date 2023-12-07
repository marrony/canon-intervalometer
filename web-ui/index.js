htmx.on("htmx:after-swap", function(evt) {
  if (evt.target && evt.target.nodeName === "INPUT")
    evt.target.focus();
});
