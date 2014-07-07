/* vim: set ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that coordinates can be changed programatically in the CubicBezierWidget

const TEST_URI = "chrome://browser/content/devtools/cubic-bezier-frame.xhtml";
const {CubicBezierWidget, PREDEFINED} =
  devtools.require("devtools/shared/widgets/CubicBezierWidget");

let test = Task.async(function*() {
  yield promiseTab(TEST_URI);

  let container = content.document.querySelector("#container");
  let w = new CubicBezierWidget(container, PREDEFINED.linear);

  yield coordinatesCanBeChangedByProvidingAnArray(w);
  yield coordinatesCanBeChangedByProvidingAValue(w);

  w.destroy();
  gBrowser.removeCurrentTab();
  finish();
});

function* coordinatesCanBeChangedByProvidingAnArray(widget) {
  info("Listening for the update event");
  let onUpdated = widget.once("updated");

  info("Setting new coordinates");
  widget.coordinates = [0,1,1,0];

  let bezier = yield onUpdated;
  ok(true, "The updated event was fired as a result of setting coordinates");

  is(bezier.P1[0], 0, "The new P1 time coordinate is correct");
  is(bezier.P1[1], 1, "The new P1 progress coordinate is correct");
  is(bezier.P2[0], 1, "The new P2 time coordinate is correct");
  is(bezier.P2[1], 0, "The new P2 progress coordinate is correct");
}

function* coordinatesCanBeChangedByProvidingAValue(widget) {
  info("Listening for the update event");
  let onUpdated = widget.once("updated");

  info("Setting linear css value");
  widget.cssCubicBezierValue = "linear";
  let bezier = yield onUpdated;
  ok(true, "The updated event was fired as a result of setting cssValue");

  is(bezier.P1[0], 0, "The new P1 time coordinate is correct");
  is(bezier.P1[1], 0, "The new P1 progress coordinate is correct");
  is(bezier.P2[0], 1, "The new P2 time coordinate is correct");
  is(bezier.P2[1], 1, "The new P2 progress coordinate is correct");

  info("Setting a custom cubic-bezier css value");
  let onUpdated = widget.once("updated");
  widget.cssCubicBezierValue = "cubic-bezier(.25,-0.5, 1, 1.45)";
  let bezier = yield onUpdated;
  ok(true, "The updated event was fired as a result of setting cssValue");

  is(bezier.P1[0], .25, "The new P1 time coordinate is correct");
  is(bezier.P1[1], -.5, "The new P1 progress coordinate is correct");
  is(bezier.P2[0], 1, "The new P2 time coordinate is correct");
  is(bezier.P2[1], 1.45, "The new P2 progress coordinate is correct");
}
