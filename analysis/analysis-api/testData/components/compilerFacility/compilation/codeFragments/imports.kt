// MODULE: context

// FILE: context.kt
import java.util.TreeSet

fun test() {
    <caret_context>val x = 0
}


// MODULE: main
// MODULE_KIND: CodeFragment
// CONTEXT_MODULE: context

// FILE: fragment.kt
// CODE_FRAGMENT_KIND: EXPRESSION
// CODE_FRAGMENT_IMPORT: java.io.File
TreeSet(File("foo").readLines())