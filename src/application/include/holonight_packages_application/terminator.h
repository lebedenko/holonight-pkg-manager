#pragma once

namespace holonight_packages_application {

// Ends the helper process after a cancel (REQ-F-056). libalpm cannot stop a running download, so a cancel accepted
// before the commit point answers `Cancelled`, then asks this port to remove the database lock file and end the
// process; the bus starts a new helper on the next call. The implementation runs after the reply has been handed to the
// bus. Tests use a recording double.
class Terminator {
 public:
  Terminator() = default;
  Terminator(const Terminator&) = delete;
  Terminator& operator=(const Terminator&) = delete;
  Terminator(Terminator&&) = delete;
  Terminator& operator=(Terminator&&) = delete;
  virtual ~Terminator();

  // Called on the service thread, after the `Cancelled` reply was produced. Must not return control to a state in which
  // the cancelled work could still change the system.
  virtual void terminateAfterReply() = 0;
};

}  // namespace holonight_packages_application
