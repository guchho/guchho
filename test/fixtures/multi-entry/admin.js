// The second of two independent entries. It touches the DOM the same way app.js
// does, which is what makes the pair useful: the two bundles must not leak
// names into each other.
function mountAdmin(root) {
    root.textContent = "admin mounted";
    console.log("admin: ready");
}

mountAdmin(document.getElementById("root"));
