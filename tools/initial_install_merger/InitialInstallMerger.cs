using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("STM32N647 Initial Install Merger")]
[assembly: System.Reflection.AssemblyVersion("1.0.0.0")]

namespace InitialInstallMerger
{
    internal sealed class Segment
    {
        public uint Address;
        public byte[] Data;
        public Segment(uint address, byte[] data) { Address = address; Data = data; }
    }

    internal static class HexFile
    {
        public static List<Segment> Read(string path)
        {
            if (!File.Exists(path)) throw new InvalidDataException("找不到文件：" + path);
            if (new FileInfo(path).Length > 96L * 1024 * 1024)
                throw new InvalidDataException("HEX 文件过大：" + path);
            var rows = new List<Segment>();
            uint upper = 0;
            bool ended = false;
            int lineNumber = 0;
            foreach (string original in File.ReadLines(path))
            {
                lineNumber++;
                string line = original.Trim();
                if (line.Length == 0) continue;
                try
                {
                    if (ended || line[0] != ':' || line.Length < 11 || (line.Length & 1) == 0)
                        throw new FormatException();
                    int size = (line.Length - 1) / 2;
                    if (size > 260) throw new FormatException();
                    byte[] record = new byte[size];
                    int sum = 0;
                    for (int i = 0; i < size; i++)
                    {
                        record[i] = Convert.ToByte(line.Substring(1 + 2 * i, 2), 16);
                        sum += record[i];
                    }
                    int count = record[0], offset = record[1] * 256 + record[2], type = record[3];
                    if (count + 5 != size || (sum & 255) != 0) throw new FormatException();
                    if (type == 0)
                    {
                        ulong address = (ulong)upper + (uint)offset;
                        if (address + (uint)count > 0x100000000UL) throw new FormatException();
                        if (count > 0)
                        {
                            byte[] data = new byte[count];
                            Buffer.BlockCopy(record, 4, data, 0, count);
                            rows.Add(new Segment((uint)address, data));
                        }
                    }
                    else if ((type == 2 || type == 4) && count == 2 && offset == 0)
                        upper = (uint)(record[4] * 256 + record[5]) << (type == 2 ? 4 : 16);
                    else if (type == 1 && count == 0 && offset == 0) ended = true;
                    else if ((type == 3 || type == 5) && count == 4 && offset == 0) { }
                    else throw new FormatException();
                }
                catch (Exception ex)
                {
                    if (!(ex is FormatException) && !(ex is OverflowException)) throw;
                    throw new InvalidDataException(Path.GetFileName(path) + " 第 " + lineNumber + " 行 HEX 格式或校验和错误。");
                }
            }
            if (!ended || rows.Count == 0) throw new InvalidDataException(Path.GetFileName(path) + " 不完整，缺少数据或结束记录。");
            rows.Sort((a, b) => a.Address.CompareTo(b.Address));
            ulong previousEnd = 0;
            foreach (Segment row in rows)
            {
                if (row.Address < previousEnd) throw new InvalidDataException(Path.GetFileName(path) + " 含重叠地址。");
                previousEnd = (ulong)row.Address + (uint)row.Data.Length;
            }
            return rows;
        }

        public static byte[] Flatten(List<Segment> parts, uint start, uint limit, string name)
        {
            if (parts.Count == 0 || parts[0].Address != start)
                throw new InvalidDataException(String.Format("{0} 必须从 0x{1:X8} 开始。请选择独立组件 HEX。", name, start));
            foreach (Segment part in parts)
                if (part.Address < start || (ulong)part.Address + (uint)part.Data.Length > limit)
                    throw new InvalidDataException(String.Format("{0} 地址越界（允许 0x{1:X8}～0x{2:X8}）。", name, start, limit - 1));
            Segment last = parts[parts.Count - 1];
            byte[] result = Filled((int)(last.Address - start) + last.Data.Length);
            foreach (Segment part in parts)
                Buffer.BlockCopy(part.Data, 0, result, (int)(part.Address - start), part.Data.Length);
            return result;
        }

        public static byte[] Filled(int length)
        {
            byte[] bytes = new byte[length];
            for (int i = 0; i < length; i++) bytes[i] = 255;
            return bytes;
        }

        private static void Record(TextWriter writer, byte type, ushort offset, byte[] bytes, int start, int count)
        {
            int sum = count + (offset >> 8) + (offset & 255) + type;
            var line = new StringBuilder(11 + count * 2);
            line.Append(':').Append(count.ToString("X2")).Append(offset.ToString("X4")).Append(type.ToString("X2"));
            for (int i = 0; i < count; i++) { byte b = bytes[start + i]; sum += b; line.Append(b.ToString("X2")); }
            line.Append(((-sum) & 255).ToString("X2"));
            writer.WriteLine(line.ToString());
        }

        public static void Write(string path, List<Segment> parts)
        {
            parts.Sort((a, b) => a.Address.CompareTo(b.Address));
            using (var writer = new StreamWriter(path, false, new UTF8Encoding(false)))
            {
                writer.NewLine = "\n";
                uint previousUpper = UInt32.MaxValue;
                foreach (Segment part in parts)
                    for (int i = 0; i < part.Data.Length; )
                    {
                        uint address = part.Address + (uint)i, upper = address >> 16;
                        if (upper != previousUpper)
                        {
                            Record(writer, 4, 0, new byte[] { (byte)(upper >> 8), (byte)upper }, 0, 2);
                            previousUpper = upper;
                        }
                        int count = Math.Min(Math.Min(16, part.Data.Length - i), 65536 - (int)(address & 65535));
                        Record(writer, 0, (ushort)(address & 65535), part.Data, i, count);
                        i += count;
                    }
                Record(writer, 1, 0, new byte[0], 0, 0);
            }
        }
    }

    internal sealed class InstallPlan
    {
        public const uint AppStart = 0x70100400, AppLimit = 0x70AFF000, NetworkStart = 0x70200000;
        private readonly List<Segment> parts;
        public readonly string Summary;
        public readonly string[] Inputs;
        private static readonly uint[] CrcTable = MakeCrcTable();
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool MoveFileEx(string existing, string replacement, uint flags);

        private InstallPlan(List<Segment> data, string summary, string[] inputs)
        { parts = data; Summary = summary; Inputs = inputs; }

        private static uint[] MakeCrcTable()
        {
            var table = new uint[256];
            for (uint i = 0; i < table.Length; i++)
            {
                uint crc = i;
                for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ ((crc & 1) == 1 ? 0xEDB88320U : 0);
                table[i] = crc;
            }
            return table;
        }

        public static uint Crc(byte[] bytes, int count)
        {
            uint crc = UInt32.MaxValue;
            for (int i = 0; i < count; i++) crc = CrcTable[(crc ^ bytes[i]) & 255] ^ (crc >> 8);
            return ~crc;
        }

        private static void Put(byte[] bytes, int offset, uint value)
        { Buffer.BlockCopy(BitConverter.GetBytes(value), 0, bytes, offset, 4); }
        private static uint Get(byte[] bytes, int offset) { return BitConverter.ToUInt32(bytes, offset); }
        private static void Vector(byte[] bytes, uint address, string name)
        {
            if (bytes.Length < 8) throw new InvalidDataException(name + " 向量表不完整。");
            uint stack = Get(bytes, 0), reset = Get(bytes, 4);
            if (stack < 0x34000000 || stack > 0x34200000 || (stack & 7) != 0 || (reset & 1) == 0 ||
                (reset & ~1U) < address || (ulong)(reset & ~1U) >= (ulong)address + (uint)bytes.Length)
                throw new InvalidDataException(name + " 向量表或复位入口无效，不能用于这套布局。");
        }

        private static void VerifyFsbl(byte[] signed)
        {
            if (signed.Length < 0x408 || Encoding.ASCII.GetString(signed, 0, 4) != "STM2")
                throw new InvalidDataException("FSBL 缺少 STM32N6 签名头，请选择已签名的 fsbl_universal.hex。");
            if (Get(signed, 0x68) != 0x00020300 || Get(signed, 0x6C) != signed.Length - 0x240)
                throw new InvalidDataException("FSBL 签名头版本或长度错误，要求 2.3、0x400 对齐。");
            byte[] raw = new byte[signed.Length - 0x400];
            Buffer.BlockCopy(signed, 0x400, raw, 0, raw.Length);
            Vector(raw, 0x34180400, "FSBL");
            if (Get(signed, 0x70) != Get(raw, 4))
                throw new InvalidDataException("FSBL 签名头入口与实际复位入口不一致。");
            uint checksum = 0;
            for (int i = 0x240; i < signed.Length; i++) checksum = unchecked(checksum + signed[i]);
            if (checksum != Get(signed, 0x64)) throw new InvalidDataException("FSBL ROM 校验和错误。");
            string markers = Encoding.ASCII.GetString(raw);
            if (!markers.Contains("[BM] boot manager") || !markers.Contains("[BM] -> agent"))
                throw new InvalidDataException("请选择本工程 compat 的通用 FSBL；普通 SDK FSBL 不包含独立 OTA 引导。");
        }

        public static InstallPlan Create(string fsblPath, string agentPath, string appPath, string modelPath, uint profile)
        {
            if (profile > 2) throw new InvalidDataException("启动配置无效。");
            byte[] fsbl = HexFile.Flatten(HexFile.Read(fsblPath), 0x70000000, 0x700FC000, "FSBL");
            VerifyFsbl(fsbl);
            byte[] agent = HexFile.Flatten(HexFile.Read(agentPath), 0x71F00400, 0x72000000, "兼容版 Agent（旧版 Agent 不能混用）");
            Vector(agent, 0x71F00400, "Agent");
            var appParts = HexFile.Read(appPath);
            bool hasModel = !String.IsNullOrWhiteSpace(modelPath);
            byte[] application = HexFile.Flatten(appParts, AppStart, hasModel ? NetworkStart : AppLimit, "应用");
            Vector(application, AppStart, "应用");
            if (hasModel)
            {
                if (profile != 2) throw new InvalidDataException("包含 AI 模型时，请选择“SDK 99 AI · 800 MHz”启动配置。");
                var modelParts = HexFile.Read(modelPath);
                HexFile.Flatten(modelParts, NetworkStart, AppLimit, "AI 模型");
                appParts.AddRange(modelParts);
            }
            byte[] image = HexFile.Flatten(appParts, AppStart, AppLimit, "应用");
            Vector(image, AppStart, "应用");
            byte[] prefix = HexFile.Filled(0x400);
            Put(prefix, 0, 0x3254524F); Put(prefix, 4, profile);
            Put(prefix, 8, (uint)image.Length); Put(prefix, 12, Crc(image, image.Length));
            Put(prefix, 16, 0); Put(prefix, 20, 0); Put(prefix, 24, 0);
            Put(prefix, 28, Crc(prefix, 28));
            var output = new List<Segment> {
                new Segment(0x70000000, fsbl), new Segment(0x700FC000, HexFile.Filled(4096)),
                new Segment(0x70100000, prefix), new Segment(AppStart, image),
                new Segment(0x70B00000, HexFile.Filled(4096)),
                new Segment(0x71500000, HexFile.Filled(4096)), new Segment(0x71F00400, agent)
            };
            string summary = String.Format("FSBL：{0:N0} 字节，签名头与入口通过检查\r\nAgent：{1:N0} 字节，兼容布局 0x71F00400\r\n启动应用：{2:N0} 字节（{3}）\r\n启动配置：{4}，profile={5}\r\n已生成当前固件长度、CRC32 和初始化记录。",
                fsbl.Length, agent.Length, image.Length, hasModel ? "包含单独选入的模型" : "未单独选入模型",
                profile == 1 ? "600 MHz" : "800 MHz", profile);
            return new InstallPlan(output, summary,
                new[] { fsblPath, agentPath, appPath, modelPath }.Where(p => !String.IsNullOrWhiteSpace(p)).Select(Path.GetFullPath).ToArray());
        }

        public string Save(string path, bool overwrite)
        {
            string target = Path.GetFullPath(path);
            if (Inputs.Any(input => String.Equals(input, target, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException("输出文件不能覆盖输入组件，请使用 initial_install.hex 等其他文件名。");
            if (File.Exists(target) && !overwrite) throw new IOException("输出文件已存在，请另选位置或允许覆盖。");
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            string temporary = target + ".tmp-" + Guid.NewGuid().ToString("N");
            try
            {
                HexFile.Write(temporary, new List<Segment>(parts));
                // Re-read before publishing: checksum, EOF and duplicate ranges must all pass.
                HexFile.Read(temporary);
                if (File.Exists(target))
                {
                    // Rename the completed file on the same volume. Unlike
                    // ReplaceFile, MoveFileEx also works with removable FAT media.
                    if (!MoveFileEx(temporary, target, 0x1 | 0x8)) throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                else File.Move(temporary, target);
            }
            finally { if (File.Exists(temporary)) File.Delete(temporary); }
            string hash;
            using (var sha = SHA256.Create())
            using (var stream = File.OpenRead(target)) hash = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "").ToLowerInvariant();
            return Summary + "\r\n\r\n已生成：" + target + "\r\nSHA256：" + hash;
        }
    }

    internal sealed class MainForm : Form
    {
        private readonly TextBox[] paths = new TextBox[4];
        private readonly ComboBox profile = new ComboBox();
        private readonly TextBox output = new TextBox(), log = new TextBox();
        private readonly Button generate = new Button(), openFolder = new Button();
        private readonly ProgressBar progress = new ProgressBar();
        private readonly Panel fields = new Panel();
        private static readonly Color Background = Color.FromArgb(244, 247, 251), Ink = Color.FromArgb(28, 45, 66);

        public MainForm()
        {
            Text = "STM32N647 · 完整烧录包合成器";
            ClientSize = new Size(950, 760); MinimumSize = new Size(850, 790);
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.Dpi;
            Font = new Font("Microsoft YaHei UI", 10F);
            BackColor = Background; ForeColor = Ink;
            AddLabel(24, 20, 900, 36, "合成 initial_install.hex", 19F, true);
            AddLabel(26, 62, 895, 28, "选择独立固件文件，生成一份可直接烧录的完整安装包。", 10F, false);
            fields.SetBounds(24, 104, 902, 365); fields.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            fields.BackColor = Color.White; Controls.Add(fields);
            string[] labels = { "FSBL（已签名）", "独立 OTA Agent", "启动应用", "AI 模型（可选）" };
            for (int i = 0; i < 4; i++)
            {
                int row = i;
                var caption = new Label { Text = labels[i], AutoSize = false, TextAlign = ContentAlignment.MiddleLeft };
                caption.SetBounds(16, 16 + i * 52, 152, 33); fields.Controls.Add(caption);
                paths[i] = new TextBox(); paths[i].SetBounds(170, 18 + i * 52, 618, 30);
                paths[i].Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right; fields.Controls.Add(paths[i]);
                var browse = new Button { Text = "选择…", FlatStyle = FlatStyle.Flat };
                browse.SetBounds(800, 16 + i * 52, 86, 34); browse.Anchor = AnchorStyles.Top | AnchorStyles.Right;
                browse.Click += (sender, args) => BrowseInput(row); fields.Controls.Add(browse);
            }
            var clockCaption = new Label { Text = "启动配置", TextAlign = ContentAlignment.MiddleLeft };
            clockCaption.SetBounds(16, 228, 152, 34); fields.Controls.Add(clockCaption);
            profile.DropDownStyle = ComboBoxStyle.DropDownList;
            profile.Items.AddRange(new object[] { "本工程管理应用 · 800 MHz", "SDK 1～61 · 600 MHz", "SDK 99 AI · 800 MHz" });
            profile.SelectedIndex = 0; profile.SetBounds(170, 230, 618, 32);
            profile.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right; fields.Controls.Add(profile);
            var outCaption = new Label { Text = "输出目录", TextAlign = ContentAlignment.MiddleLeft };
            outCaption.SetBounds(16, 280, 152, 34); fields.Controls.Add(outCaption);
            output.SetBounds(170, 282, 618, 30); output.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            output.Text = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "output"); fields.Controls.Add(output);
            var outBrowse = new Button { Text = "选择…", FlatStyle = FlatStyle.Flat };
            outBrowse.SetBounds(800, 280, 86, 34); outBrowse.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            outBrowse.Click += (sender, args) => {
                using (var dialog = new FolderBrowserDialog { Description = "选择烧录包输出目录" })
                    if (dialog.ShowDialog(this) == DialogResult.OK) output.Text = dialog.SelectedPath;
            }; fields.Controls.Add(outBrowse);
            var help = new Label { Text = "FSBL 和 Agent 使用本工程 compat 组件。AI 模型可留空，HEX 自带地址。", ForeColor = Color.FromArgb(103, 117, 136) };
            help.SetBounds(16, 331, 870, 26); fields.Controls.Add(help);
            AddLabel(26, 480, 900, 26, "完整安装会替换当前应用，并初始化升级暂存、回滚及启动记录。", 10F, false);
            generate.Text = "合成 initial_install.hex"; generate.SetBounds(24, 521, 330, 48);
            generate.BackColor = Color.FromArgb(28, 123, 89); generate.ForeColor = Color.White;
            generate.FlatStyle = FlatStyle.Flat; generate.FlatAppearance.BorderSize = 0;
            generate.Click += Generate; Controls.Add(generate);
            openFolder.Text = "打开输出目录"; openFolder.SetBounds(372, 521, 166, 48);
            openFolder.FlatStyle = FlatStyle.Flat; openFolder.Click += (sender, args) => OpenOutput(); Controls.Add(openFolder);
            progress.SetBounds(24, 583, 902, 7); progress.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            Controls.Add(progress);
            log.SetBounds(24, 606, 902, 130); log.Multiline = true; log.ReadOnly = true; log.ScrollBars = ScrollBars.Vertical;
            log.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            log.BackColor = Color.White; log.BorderStyle = BorderStyle.FixedSingle;
            log.Text = "准备就绪。\r\n默认使用随工具附带的固件，选择输出目录后点击合成。\r\n工具只生成文件，烧录请使用 STM32CubeProgrammer 与板级 external loader。";
            Controls.Add(log); LoadDefaults();
        }

        private void AddLabel(int x, int y, int width, int height, string text, float size, bool bold)
        {
            var label = new Label { Text = text, Font = new Font(Font.FontFamily, size, bold ? FontStyle.Bold : FontStyle.Regular) };
            label.SetBounds(x, y, width, height); label.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            Controls.Add(label);
        }

        private void LoadDefaults()
        {
            string location = AppDomain.CurrentDomain.BaseDirectory;
            string project = Path.GetFullPath(Path.Combine(location, "../../Binary/compat"));
            foreach (string folder in new[] { project, Path.Combine(location, "firmware"), location })
            {
                string[] names = { "fsbl_universal.hex", "agent.hex", "appli.hex" };
                if (!names.All(name => File.Exists(Path.Combine(folder, name)))) continue;
                for (int i = 0; i < names.Length; i++) paths[i].Text = Path.Combine(folder, names[i]);
                log.Text = (folder == project ? "已选用工程内当前 Binary/compat 固件。" : "已选用随工具附带的固件。") +
                    "\r\n选择输出目录后点击合成。\r\n生成的 HEX 使用 STM32CubeProgrammer 与板级 external loader 烧录。";
                return;
            }
            log.Text = "请选择 FSBL、Agent 和应用 HEX。AI 模型文件可留空。";
        }

        private void BrowseInput(int row)
        {
            using (var dialog = new OpenFileDialog { Filter = "Intel HEX 固件 (*.hex)|*.hex", Title = "选择固件 HEX", CheckFileExists = true })
                if (dialog.ShowDialog(this) == DialogResult.OK)
                {
                    paths[row].Text = dialog.FileName;
                    if (row == 3) profile.SelectedIndex = 2;
                }
        }

        private void OpenOutput()
        {
            try
            {
                string folder = Path.GetFullPath(output.Text.Trim());
                Directory.CreateDirectory(folder);
                Process.Start(new ProcessStartInfo("explorer.exe", "\"" + folder + "\"") { UseShellExecute = true });
            }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, "无法打开目录", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }

        private void Generate(object sender, EventArgs args)
        {
            string target;
            try { target = Path.Combine(Path.GetFullPath(output.Text.Trim()), "initial_install.hex"); }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, "输出目录无效"); return; }
            if (File.Exists(target) && MessageBox.Show(this, "将更新以下文件：\r\n" + target, "覆盖已有合成文件？", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
            string[] input = paths.Select(box => box.Text.Trim()).ToArray();
            uint selectedProfile = (uint)profile.SelectedIndex;
            fields.Enabled = false; generate.Enabled = false; openFolder.Enabled = false;
            progress.Style = ProgressBarStyle.Marquee;
            log.Text = "正在校验固件并合成，请稍候…";
            var worker = new BackgroundWorker();
            worker.DoWork += (source, work) => work.Result = InstallPlan.Create(input[0], input[1], input[2], input[3], selectedProfile).Save(target, true);
            worker.RunWorkerCompleted += (source, done) => {
                fields.Enabled = true; generate.Enabled = true; openFolder.Enabled = true;
                progress.Style = ProgressBarStyle.Continuous;
                progress.Value = done.Error == null ? 100 : 0;
                if (done.Error != null) log.Text = "合成失败：" + done.Error.Message;
                else log.Text = done.Result.ToString();
                worker.Dispose();
            };
            worker.RunWorkerAsync();
        }
    }

    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            try
            {
                if (args.Length == 0 || (args.Length == 2 && args[0] == "--preview"))
                {
                    Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
                    using (var form = new MainForm())
                    {
                        if (args.Length == 0) Application.Run(form);
                        else
                        {
                            // Materialize native child windows without displaying
                            // a visible window or stealing the user's taskbar.
                            form.Opacity = 0;
                            form.ShowInTaskbar = false;
                            form.Show(); Application.DoEvents(); form.Refresh();
                            using (var bitmap = new Bitmap(form.Width, form.Height))
                            { form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size)); bitmap.Save(args[1], System.Drawing.Imaging.ImageFormat.Png); }
                            form.Close();
                        }
                    }
                    return 0;
                }
                var options = new Dictionary<string, string>(StringComparer.Ordinal);
                // WinExe has no console handle. Write UTF-8 to redirected pipes
                // without changing the console code page (which would fail).
                Console.SetOut(new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false)) { AutoFlush = true });
                Console.SetError(new StreamWriter(Console.OpenStandardError(), new UTF8Encoding(false)) { AutoFlush = true });
                bool overwrite = false;
                for (int i = 0; i < args.Length; i++)
                {
                    if (args[i] == "--overwrite") { overwrite = true; continue; }
                    if (args[i] == "--help")
                    {
                        Console.WriteLine("InitialInstallMerger.exe --fsbl FILE --agent FILE --app FILE [--model FILE] [--profile default|sdk600|ai800] --out FILE [--overwrite]");
                        return 0;
                    }
                    if (!new[] { "--fsbl", "--agent", "--app", "--model", "--profile", "--out" }.Contains(args[i]) || i + 1 >= args.Length || options.ContainsKey(args[i]))
                        throw new ArgumentException("命令行参数无效：" + args[i]);
                    options[args[i]] = args[++i];
                }
                foreach (string key in new[] { "--fsbl", "--agent", "--app", "--out" })
                    if (!options.ContainsKey(key)) throw new ArgumentException("缺少参数：" + key);
                string name = options.ContainsKey("--profile") ? options["--profile"] : "default";
                if (name != "default" && name != "sdk600" && name != "ai800") throw new ArgumentException("启动配置必须为 default、sdk600 或 ai800。");
                uint profile = name == "sdk600" ? 1U : name == "ai800" ? 2U : 0U;
                var plan = InstallPlan.Create(options["--fsbl"], options["--agent"], options["--app"], options.ContainsKey("--model") ? options["--model"] : null, profile);
                Console.WriteLine(plan.Save(options["--out"], overwrite));
                return 0;
            }
            catch (Exception ex) { Console.Error.WriteLine(ex.Message); return 2; }
        }
    }
}
