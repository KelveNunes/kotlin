// MODULE: context

// FILE: context.kt
class Test {
    private var a: String = "foo"

    fun test() {
        <caret_context>val x = 0
    }
}


// MODULE: main
// MODULE_KIND: CodeFragment
// CONTEXT_MODULE: context

// FILE: fragment.kt
// CODE_FRAGMENT_KIND: EXPRESSION
@Suppress("INVISIBLE_SETTER")
a = "bar"