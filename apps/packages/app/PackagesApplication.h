#pragma once

#include <QGuiApplication>

#include <memory>

class QQuickView;
class InstalledPackagesModel;
class UpdatesModel;

class PackagesApplication : public QGuiApplication {
  Q_OBJECT

 public:
  PackagesApplication(int& argc, char** argv);
  ~PackagesApplication() override;

  PackagesApplication(const PackagesApplication&) = delete;
  PackagesApplication& operator=(const PackagesApplication&) = delete;
  PackagesApplication(PackagesApplication&&) = delete;
  PackagesApplication& operator=(PackagesApplication&&) = delete;

  [[nodiscard]] bool isReady() const;

 private:
  // view_ holds QML-facing references to both models (see setInitialProperties in the
  // constructor), so it must be destroyed first; the destructor enforces this explicitly
  // rather than relying on reverse declaration order.
  std::unique_ptr<InstalledPackagesModel> installed_packages_model_;
  std::unique_ptr<UpdatesModel> updates_model_;
  std::unique_ptr<QQuickView> view_;
  bool ready_ = false;
};
