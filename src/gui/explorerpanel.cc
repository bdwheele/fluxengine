#include "lib/globals.h"
#include "lib/fluxmap.h"
#include "lib/environment.h"
#include "lib/fluxsource/fluxsource.h"
#include "lib/decoders/decoders.h"
#include "lib/proto.h"
#include "gui.h"
#include "layout.h"
#include "jobqueue.h"

static Bytes fakeBits(const std::vector<bool>& bits)
{
	Bytes result;
	ByteWriter bw(result);

	auto it = bits.begin();
	while (it != bits.end())
	{
		uint8_t b = (*it++) << 4;
		if (it != bits.end())
			b |= *it++;
		bw.write_8(b);
	}

	return result;
}

class ExplorerPanelImpl :
    public ExplorerPanelGen,
    public ExplorerPanel,
    JobQueue
{
    enum
    {
        STATE_DEAD,
        STATE_WORKING,
        STATE_IDLE,
    };

public:
    ExplorerPanelImpl(MainWindow* mainWindow, wxSimplebook* parent):
        ExplorerPanelGen(parent),
        ExplorerPanel(mainWindow)
    {
        parent->AddPage(this, "explorer");
    }

public:
    void Start() override
    {
        try
        {
            SetPage(MainWindow::PAGE_EXPLORER);
            PrepareConfig();

            SetState(STATE_IDLE);

            _explorerFluxmap = nullptr;
            _explorerTrack = -1;
            _explorerSide = -1;
            _explorerUpdatePending = false;

            UpdateExplorerData();
        }
        catch (const ErrorException& e)
        {
            wxMessageBox(e.message, "Error", wxOK | wxICON_ERROR);
            StartIdle();
        }
    }

    void UpdateState()
    {
        explorerToolbar->EnableTool(
            explorerBackTool->GetId(), _state == STATE_IDLE);
        explorerToolbar->Refresh();
    }

    void OnBackButton(wxCommandEvent&) override
    {
        StartIdle();
    }

private:
    void SetState(int state)
    {
        if (state != _state)
        {
            _state = state;
            CallAfter(
                [&]()
                {
                    UpdateState();
                });
        }
    }

    void SwitchFrom() override
    {
        SetState(STATE_DEAD);
    }

private:
    void OnExplorerSettingChange(wxSpinEvent& event) override
    {
        UpdateExplorerData();
    }

    void OnExplorerSettingChange(wxSpinDoubleEvent& event) override
    {
        UpdateExplorerData();
    }

    void OnExplorerSettingChange(wxCommandEvent& event) override
    {
        UpdateExplorerData();
    }

    void OnExplorerRefreshButton(wxCommandEvent& event) override
    {
        _explorerFluxmap = nullptr;
        _explorerTrack = -1;
        _explorerSide = -1;
        _explorerUpdatePending = false;

        UpdateExplorerData();
    }

    void OnGuessClockButton(wxCommandEvent& event) override
    {
        nanoseconds_t clock = histogram->GetMedian();
        explorerClockSpinCtrl->SetValue(clock / 1e3);
        UpdateExplorerData();
    }

    void OnQueueEmpty() override
    {
        SetState(STATE_IDLE);
    }

    void QueueJob(std::function<void(void)> f)
    {
        SetState(STATE_WORKING);
        JobQueue::QueueJob(f);
    }

    void UpdateExplorerData()
    {
        if (!IsQueueEmpty())
        {
            _explorerUpdatePending = true;
            return;
        }

        _explorerUpdatePending = false;
        QueueJob(
            [this]()
            {
                /* You need to call this if the config changes to invalidate
                 * any caches. */

                Environment::reset();

                int desiredTrack = explorerTrackSpinCtrl->GetValue();
                int desiredSide = explorerSideSpinCtrl->GetValue();
                if (!_explorerFluxmap || (desiredTrack != _explorerTrack) ||
                    (desiredSide != _explorerSide))
                {
                    auto fluxSource = FluxSource::create(config.flux_source());
                    _explorerFluxmap =
                        fluxSource->readFlux(desiredTrack, desiredSide)->next();
                    _explorerTrack = desiredTrack;
                    _explorerSide = desiredSide;
                }

                runOnUiThread(
                    [&]()
                    {
                        _state = STATE_IDLE;
                        UpdateState();

                        FluxmapReader fmr(*_explorerFluxmap);
                        fmr.seek(explorerStartTimeSpinCtrl->GetValue() * 1e6);

                        nanoseconds_t clock =
                            explorerClockSpinCtrl->GetValue() * 1e3;
                        FluxDecoder fluxDecoder(&fmr, clock, DecoderProto());
                        fluxDecoder.readBits(
                            explorerBitOffsetSpinCtrl->GetValue());
                        auto bits = fluxDecoder.readBits();

                        Bytes bytes;
                        switch (explorerDecodeChoice->GetSelection())
                        {
							case 0:
								bytes = fakeBits(bits);
								break;

                            case 1:
                                bytes = toBytes(bits);
                                break;

                            case 2:
                                bytes = decodeFmMfm(bits.begin(), bits.end());
                                break;
                        }

                        if (explorerReverseCheckBox->GetValue())
                            bytes = bytes.reverseBits();

                        std::stringstream s;
                        hexdump(s, bytes);
                        explorerText->SetValue(s.str());

                        histogram->Redraw(*_explorerFluxmap, clock);

                        if (_explorerUpdatePending)
                            UpdateExplorerData();
                    });
            });
    }

private:
    int _state = STATE_DEAD;
    int _explorerTrack;
    int _explorerSide;
    bool _explorerUpdatePending;
    std::unique_ptr<const Fluxmap> _explorerFluxmap;
};

ExplorerPanel* ExplorerPanel::Create(
    MainWindow* mainWindow, wxSimplebook* parent)
{
    return new ExplorerPanelImpl(mainWindow, parent);
}
