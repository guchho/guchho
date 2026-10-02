// One of two independent entries. No shared file with admin.js, so each has to
// build on its own without either depending on the other.

function mountApp(root) {
    root.textContent = "app mounted";
    console.log("app: ready");
}

mountApp(document.getElementById("root"));
