// "virtual:fixture" has no file behind it. Nothing on disk resolves it, so this
// fixture only builds if a plugin's onResolve or onLoad hook answers the
// specifier — which is the property worth asserting: a build that fails here
// shows the hook never fired, and a build that succeeds has nothing to prove
// about what the plugin returned.
import banner from "virtual:fixture";
import { theme } from "virtual:config";

const heading = document.createElement("h1");
heading.textContent = banner;
heading.className = theme.headingClass;
document.body.appendChild(heading);
