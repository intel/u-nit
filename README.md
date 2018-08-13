# u-nit

init is the program responsible to set up userspace - mount filesystems,
start services and reap process that die.

Some features that this init implementation provide (or intends to
provide, see [TODO](TODO)):

    - Processes can be started in a specific order;
    - Processes can be deemed safe; special track of them is
      provided, such as the ability to start a "safe-mode" application in
      case it crashes;
    - Process can be tied to a specific processor core;
    - Simple [inittab](specs/inittab-spec.txt) file to define process;
    - Follow [MISRA-C](https://www.misra.org.uk/MISRAHome/MISRAC2012/tabid/196/Default.aspx) guidelines to enhance safety of code;
    - Thorough testing - coverage guided and providing mocks to test
    error handling code.

## Building

u-nit requires glibc >= 2.9 and a Linux environment because it uses
Linux-specific functions. Other than that, a simple `make` should
build and generate init executable.

## Testing

Automatic tests are provided on the [tests](tests) directory. They can be
invoked from Makefile. Some tests require QEMU - as well as a kernel
and some root filesystems (not provided). See tests [README](tests/README.tests)
for more information.

## Hacking

Code should be compliant with MISRA-C guidelines. Note however that not
all guidelines are being strictly followed right now: it is still being
evaluated which guidelines will be followed and which will be deviated.
In this case, a formal deviation process will be followed.

Regarding code-style, u-nit loosely follows Linux kernel
guidelines. [clang-format](http://clang.llvm.org/docs/ClangFormat.html)
is used to enforce code style. Simply run `make format-code` and the code
will be formatted accordingly.

## Contributing

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.

## Security issues

Please report any security issues with this code to https://01.org/security

## Disclaimer

The software may be used to create end products used in safety-critical
applications designed to comply with functional safety standards or
requirements (“Safety-Critical Applications”), however, it may not be
qualified for use in all applications in which its failure could create
a safety hazard or any situation where personal injury or death may
occur, including but not limited to medical, life-saving or life
sustaining systems, transportation systems, manned or unmanned vehicles,
nuclear systems, or in combination with any potentially hazardous
product. It is licensee’s responsibility to design, manage and assure
system-level safeguards to anticipate, monitor and control system
failures, and licensee is solely responsible for all applicable
regulatory standards and safety-related requirements concerning
licensee’s use of the software in Safety Critical Applications.
