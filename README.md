# CompositeInput vcomponent

This component is the CompositeInput vcomponent skeleton for the released RDK HAL AIDL interface:

- Module: `compositeinput`
- Interface release: `0.2.0.0`
- Package: `com.rdk.hal.compositeinput`
- Binder service: `composite_input`
- Reference: <https://github.com/rdkcentral/rdk-halif-aidl/tree/develop/compositeinput/0.2.0.0>

## Included migration work

- Build integration now uses the standalone `compositeinput@0.2.0.0` HAL module.
- The installed HFP profile is `hfp-compositeinput.yaml`.
- The service publishes `ICompositeInputManager` under the interface-defined
  `composite_input` service name.
- The manager reports the two HFP-configured port IDs (`0` and `1`) and the
  platform’s single-concurrent-port limit.

## Product-specific implementation boundary

The RDK interface separates manager, port, controller, controller-listener, and
event-listener responsibilities. This skeleton establishes the manager service
and module integration. A product implementation must add concrete
`ICompositeInputPort` and `ICompositeInputController` implementations to connect
the HFP-declared ports to the platform’s composite-video hardware and implement
the open/start/stop/close lifecycle.

## Build

```sh
./build.sh Target=linux
```

The script checks out/builds the Binder SDK, `common@0.2.0.0`, and
`compositeinput@0.2.0.0`, then builds and installs this component under
`build/out/`.
