# Lab 1
<details>
<summary>Environment</summary>

    $ uname -a
    Linux manjaro 4.19.69-1-MANJARO #1 SMP PREEMPT Thu Aug 29 08:51:46 UTC 2019 x86_64 GNU/Linux

    $ lsb_release -a
    LSB Version:    n/a
    Distributor ID: ManjaroLinux
    Description:    Manjaro Linux
    Release:        18.1.0
    Codename:       Juhraya

    $ flex --version
    flex 2.6.4

    $ gcc --version
    gcc (GCC) 9.1.0

</details>

## Design
Speaking of flex rules design, all of them are quite easy except for `COMMENT`. For keywords, operators and end of line, matching the string literal is enough, and don't forget `pos_start` and `pos_end`, which I use a macro `BUMP_POS(n)` to change. The regex for blanks, numbers and identifiers are very trivial.

Designing the regex of `COMMENT` is a little more difficult. Here I will do a simple explanation of the regex.


     regular char (not '/' or '*')    prevent '/' after '*'s
                  |                           |
                  v                           v
         \/\*  ( [^*/]   |   [^*]\/+   |   \*+[^/] )*   \*+\/
          ^                     ^                         ^
          |                     |                         |
    starting sign     prevent '*' before '/'s        ending sign

There is also a trap in comments, which is how the `lines` and `pos`es should be changed. Comments being the only token able to cross lines, it is necessary to track how many lines it is crossing and how many characters are after the final new line.

## Problems encountered
### syntax
At the beginning, with the lack of documentation read, the misunderstanding of the syntax resulted in me putting regexes in double quotes, which was a mistake that took me a lot of time to realize and fix.

The need to escape `/` was also beyond me at first because in previous regex-writing experiences, `/` has always been a normal character. As a result, I was constantly getting this error:
```
lexical_analyzer.l:68: unrecognized rule
```
but I didn't know why it had happened. After reading [this part](https://ftp.gnu.org/old-gnu/Manuals/flex-2.5.4/html_mono/flex.html#SEC7) of the documentation and learning `r/s` has a special meaning, I realized that `/` is a character that has to be escaped.

### `COMMENT`
My first version of regex matching comments is like this:
```
\/\*([^*]|\*+[^/])*\*+\/
```
The problem occurs when we have `**/` ending comments (i.e. multiple asteroids before the slash) not being the last one. For example:

    /* comment **/
    int
    /* other */

The `int` will not be detected because all three lines above are considered a comment.

This problem exists because the asteroids are matched by `\*+[^/]` and `/` are matched by `[^*]`. Not having a non-greedy matching syntax, flex cannot be told to match `**/` with `\*+\/`. Adding `[^*]\/+` and changing `[^*]` to `[^*/]` fix it because we stop regarding `/` as a "normal character", as in the final regex, it is specified that there should not be any `*` before `/`.

## Summary
I think the design of this lab is kind of weird. The directories of input and output sources are hard coded in the C function `analyzer`, disabling it from being used in a general case. Also, test cases detection and the files to which the output goes is managed by C code, while we have to use the `diff` command in a shell to verify the correctness.

*In my opinion*, the `analyzer` function should just read `stdin` and print to `stdout`, with the `main` function being a single call to `analyzer`. This way we can use a shell script to detect test cases, manage output redirections (which is A LOT simpler!) and `diff` the result with that of the TA's. I slightly changed the `analyzer` function so that it can read `stdin` and `stdout` if the corresponding file name is `NULL`. This change should not make a difference in the test result on TA's machine since the call to `analyzer` in the current `main` function is always with non-`NULL` file names as its arguments.

As for the time spent on this lab, I spent one hour or so finishing writing regexes except for the bug in comment with most of the time figuring out the syntax of flex and then spent not much time finishing `main` and `get_all_testcase`. I was designing test cases as I was writing the code so I will not count its time.