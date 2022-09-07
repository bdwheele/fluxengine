#include "lib/globals.h"
#include "lib/vfs/sectorinterface.h"
#include "lib/imagereader/imagereader.h"
#include "lib/imagewriter/imagewriter.h"
#include "lib/image.h"

class ImageSectorInterface : public SectorInterface
{
public:
    ImageSectorInterface(std::shared_ptr<ImageReader> reader,
        std::shared_ptr<ImageWriter> writer):
        _image(reader->readImage()),
        _reader(reader),
        _writer(writer)
    {
    }

public:
    std::shared_ptr<const Sector> get(
        unsigned track, unsigned side, unsigned sectorId)
    {
        return _image->get(track, side, sectorId);
    }

    std::shared_ptr<Sector> put(
        unsigned track, unsigned side, unsigned sectorId)
    {
        _changed = true;
        return _image->put(track, side, sectorId);
    }

    virtual bool isReadOnly()
    {
        return (_writer == nullptr);
    }

    bool needsFlushing() override
    {
        return _changed;
    }

    void flushChanges() override
    {
        _writer->writeImage(*_image);
        _changed = false;
    }

    void discardChanges() override
    {
        _image = _reader->readImage();
        _changed = false;
    }

private:
    std::shared_ptr<Image> _image;
    std::shared_ptr<ImageReader> _reader;
    std::shared_ptr<ImageWriter> _writer;
    bool _changed = false;
};

std::unique_ptr<SectorInterface> SectorInterface::createImageSectorInterface(
    std::shared_ptr<ImageReader> reader, std::shared_ptr<ImageWriter> writer)
{
    return std::make_unique<ImageSectorInterface>(reader, writer);
}
