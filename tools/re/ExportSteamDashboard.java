import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class ExportSteamDashboard extends GhidraScript {
    @Override
    public void run() throws Exception {
        String output = getScriptArgs().length > 0 ? getScriptArgs()[0] : "vrdashboard-decomp.txt";
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        Set<Function> selected = new LinkedHashSet<>();
        for (FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true); it.hasNext();) {
            Function f = it.next();
            String name = f.getName();
            String lowerName = name.toLowerCase();
            if (name.equals("main") || lowerName.contains("dashboard")) selected.add(f);
            for (InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true); ii.hasNext();) {
                Instruction ins = ii.next();
                if (!ins.getFlowType().isCall()) continue;
                for (Reference ref : ins.getReferencesFrom()) {
                    Function callee = currentProgram.getFunctionManager().getFunctionAt(ref.getToAddress());
                    if (callee != null && callee.getName().toLowerCase().contains("vr")) selected.add(f);
                }
            }
        }
        for (DataIterator it = currentProgram.getListing().getDefinedData(true); it.hasNext();) {
            Data data = it.next();
            Object value = data.getValue();
            if (!(value instanceof String)) continue;
            String text = ((String) value).toLowerCase();
            if (!(text.contains("dashboard") || text.contains("laser") ||
                  text.contains("curvat") || text.contains("steam frame") ||
                  text.contains("steamframe"))) continue;
            for (Reference ref : currentProgram.getReferenceManager().getReferencesTo(data.getAddress())) {
                Function owner = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                if (owner != null) selected.add(owner);
            }
        }
        Set<Function> frontier = new LinkedHashSet<>(selected);
        for (int depth = 0; depth < 4 && !frontier.isEmpty(); depth++) {
            Set<Function> next = new LinkedHashSet<>();
            for (Function callee : frontier) {
                for (Reference ref : currentProgram.getReferenceManager().getReferencesTo(callee.getEntryPoint())) {
                    if (!ref.getReferenceType().isCall()) continue;
                    Function caller = currentProgram.getFunctionManager().getFunctionContaining(ref.getFromAddress());
                    if (caller != null && selected.add(caller)) next.add(caller);
                }
            }
            frontier = next;
        }
        try (PrintWriter out = new PrintWriter(new FileWriter(output))) {
            out.println("Binary: " + currentProgram.getName());
            out.println("Selected functions: " + selected.size());
            for (Function f : selected) {
                DecompileResults result = decomp.decompileFunction(f, 60, ghidra.util.task.TaskMonitor.DUMMY);
                out.println("\n\n===== " + f.getName() + " @ " + f.getEntryPoint() + " =====");
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) out.println(result.getDecompiledFunction().getC());
                else out.println("DECOMPILE ERROR: " + result.getErrorMessage());
            }
        }
        decomp.dispose();
        System.out.println("Wrote " + selected.size() + " decompiled functions to " + output);
    }
}
