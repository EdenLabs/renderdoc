import renderdoc as rd
import rdtest

class VK_Resource_Usage(rdtest.TestCase):
    demos_test_name = 'VK_Resource_Usage'
    resourceUsages = {}

    def check_resource_usage(self, res: rd.ResourceDescription, expectedUsages=[]):
        usages = self.resourceUsages[res.resourceId]
        if len(usages) != len(expectedUsages):
            for u in usages:
                rdtest.log.print(f"Resource '{res.name}' {res.resourceId} usage EID:{u.eventId} usage:{u.usage.name}")
            raise rdtest.TestFailureException(f"'{res.name}' {res.resourceId} Incorrect resource usages count expected:{len(expectedUsages)} actual:{len(usages)}")
        for i, u in enumerate(usages):
            eid, usage = expectedUsages[i]
            if u.usage != usage:
                raise rdtest.TestFailureException(f"'{res.name}' {res.resourceId} EID:{u.eventId} Incorrect resource usage expected:{usage.name} actual:{u.usage.name}")
            if u.eventId != eid:
                raise rdtest.TestFailureException(f"'{res.name}' {res.resourceId} usage:{u.usage.name} Incorrect resource usage EID expected:{eid} actual:{u.eventId}")

    def check_capture(self):
        # Cache the resource usage before running any replay i.e. without calling SetFrameEvent
        resources = self.controller.GetResources()
        for res in resources:
            self.resourceUsages[res.resourceId] = self.controller.GetUsage(res.resourceId)

        nestedSecondaries = self.find_action("Nested Secondary Command Buffer") is not None
        rdtest.log.print(f"Has Nested Secondary Command Buffer: {'Yes' if nestedSecondaries else 'No'}")
        descBuffer = self.find_action("Descriptor Buffer") is not None
        rdtest.log.print(f"Has Descriptor Buffer: {'Yes' if descBuffer else 'No'}")
        countNested = 35 if nestedSecondaries else 0
        countDescBuffer = 18 if descBuffer else 0

        action = self.find_action("Draw")
        self.controller.SetFrameEvent(action.eventId, False)
        swapImage = self.controller.GetPipelineState().GetOutputTargets()[0].resource

        for res in self.controller.GetResources():
            expectedUsage = []
            if res.type == rd.ResourceType.Device:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Queue:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Pool:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.SwapchainImage:
                # the swap chain image has usage, anything else does not
                if res.resourceId == swapImage:
                    expectedUsage = [(6,rd.ResourceUsage.Barrier), 
                                     (6,rd.ResourceUsage.Discard), 
                                     (7,rd.ResourceUsage.Clear), 
                                     (8,rd.ResourceUsage.Barrier), 
                                     (32,rd.ResourceUsage.ColorTarget), 
                                     (35,rd.ResourceUsage.ColorTarget), 
                                     (42,rd.ResourceUsage.ColorTarget), 
                                     (45,rd.ResourceUsage.ColorTarget), 
                                     (59,rd.ResourceUsage.ColorTarget), 
                                     (62,rd.ResourceUsage.ColorTarget), 
                                     (104,rd.ResourceUsage.ColorTarget), 
                                     (105,rd.ResourceUsage.ColorTarget), 
                                     (106,rd.ResourceUsage.ColorTarget), 
                                     (107,rd.ResourceUsage.ColorTarget), 
                                     (112,rd.ResourceUsage.ColorTarget), 
                                     (113,rd.ResourceUsage.ColorTarget), 
                                     (114,rd.ResourceUsage.ColorTarget), 
                                     (145,rd.ResourceUsage.ColorTarget), 
                                     (146,rd.ResourceUsage.ColorTarget), 
                                     (147,rd.ResourceUsage.ColorTarget), 
                                     (148,rd.ResourceUsage.ColorTarget), 
                                     (153,rd.ResourceUsage.ColorTarget), 
                                     (154,rd.ResourceUsage.ColorTarget), 
                                     (155,rd.ResourceUsage.ColorTarget)] 
                    if nestedSecondaries:
                        expectedUsage += [
                                     (175,rd.ResourceUsage.ColorTarget), 
                                     (178,rd.ResourceUsage.ColorTarget)] 
                    if descBuffer:
                        expectedUsage += [
                                     (170+countNested,rd.ResourceUsage.ColorTarget), 
                                     (173+countNested,rd.ResourceUsage.ColorTarget)] 

                    expectedUsage += [(162+countNested+countDescBuffer,rd.ResourceUsage.Barrier)]
                else:
                    expectedUsage = []
            elif res.type == rd.ResourceType.RenderPass:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Sync:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.View:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Memory:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.ShaderBinding:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Shader:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.PipelineState:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Buffer:
                if (res.name == "Vertex Buffer"):
                    expectedUsage = [(32,rd.ResourceUsage.VertexBuffer), 
                                     (35,rd.ResourceUsage.VertexBuffer),
                                     (42,rd.ResourceUsage.VertexBuffer),
                                     (45,rd.ResourceUsage.VertexBuffer),
                                     (59,rd.ResourceUsage.VertexBuffer), 
                                     (62,rd.ResourceUsage.VertexBuffer), 
                                     (104,rd.ResourceUsage.VertexBuffer), 
                                     (105,rd.ResourceUsage.VertexBuffer), 
                                     (106,rd.ResourceUsage.VertexBuffer), 
                                     (107,rd.ResourceUsage.VertexBuffer), 
                                     (112,rd.ResourceUsage.VertexBuffer), 
                                     (113,rd.ResourceUsage.VertexBuffer), 
                                     (114,rd.ResourceUsage.VertexBuffer), 
                                     (145,rd.ResourceUsage.VertexBuffer), 
                                     (146,rd.ResourceUsage.VertexBuffer), 
                                     (147,rd.ResourceUsage.VertexBuffer), 
                                     (148,rd.ResourceUsage.VertexBuffer), 
                                     (153,rd.ResourceUsage.VertexBuffer), 
                                     (154,rd.ResourceUsage.VertexBuffer), 
                                     (155,rd.ResourceUsage.VertexBuffer)] 
                    if nestedSecondaries:
                        expectedUsage += [
                                     (175,rd.ResourceUsage.VertexBuffer), 
                                     (178,rd.ResourceUsage.VertexBuffer)]
                    if descBuffer:
                        expectedUsage += [
                                     (170+countNested,rd.ResourceUsage.VertexBuffer), 
                                     (173+countNested,rd.ResourceUsage.VertexBuffer)]
                if (res.name == "Index Buffer"):
                    expectedUsage = [(35,rd.ResourceUsage.IndexBuffer),
                                     (45,rd.ResourceUsage.IndexBuffer),
                                     (62,rd.ResourceUsage.IndexBuffer),
                                     (112,rd.ResourceUsage.IndexBuffer),
                                     (113,rd.ResourceUsage.IndexBuffer),
                                     (114,rd.ResourceUsage.IndexBuffer),
                                     (153,rd.ResourceUsage.IndexBuffer),
                                     (154,rd.ResourceUsage.IndexBuffer),
                                     (155,rd.ResourceUsage.IndexBuffer)]
                    if nestedSecondaries:
                        expectedUsage += [
                                     (178,rd.ResourceUsage.IndexBuffer)]
                    if descBuffer:
                        expectedUsage += [
                                     (173+countNested,rd.ResourceUsage.IndexBuffer)]
                if (res.name == "Compute Buffer In"):
                    expectedUsage += [(73,rd.ResourceUsage.CS_Constants),
                                     (80,rd.ResourceUsage.CS_Constants)]
                    if nestedSecondaries:
                        expectedUsage += [(191,rd.ResourceUsage.CS_Constants)]
                    if descBuffer:
                        expectedUsage += [(178+countNested,rd.ResourceUsage.CS_Constants)]
                if (res.name == "Compute Buffer Out"):
                    expectedUsage += [(73,rd.ResourceUsage.CS_RWResource),
                                     (80,rd.ResourceUsage.CS_RWResource)]
                    if nestedSecondaries:
                        expectedUsage += [(191,rd.ResourceUsage.CS_RWResource)]
                    if descBuffer:
                        expectedUsage += [(178+countNested,rd.ResourceUsage.CS_RWResource)]
                if (res.name == "Indirect Data"):
                    expectedUsage += [(14,rd.ResourceUsage.Barrier),
                                     (15,rd.ResourceUsage.Clear),
                                     (16,rd.ResourceUsage.Barrier),
                                     (20,rd.ResourceUsage.CS_RWResource),
                                     (21,rd.ResourceUsage.Barrier),
                                     (91,rd.ResourceUsage.CS_RWResource),
                                     (91,rd.ResourceUsage.Indirect),
                                     (92,rd.ResourceUsage.Barrier),
                                     (103,rd.ResourceUsage.Indirect),
                                     (111,rd.ResourceUsage.Indirect),
                                     (121,rd.ResourceUsage.Barrier),
                                     (122,rd.ResourceUsage.Clear),
                                     (123,rd.ResourceUsage.Barrier),
                                     (127,rd.ResourceUsage.CS_RWResource),
                                     (130,rd.ResourceUsage.CS_RWResource),
                                     (130,rd.ResourceUsage.Indirect),
                                     (131,rd.ResourceUsage.Barrier),
                                     (144,rd.ResourceUsage.Indirect),
                                     (152,rd.ResourceUsage.Indirect)]
            elif res.type == rd.ResourceType.Texture:
                if (res.name == "Offscreen MSAA Image"):
                    expectedUsage = [(11,rd.ResourceUsage.Barrier), 
                                     (11,rd.ResourceUsage.Discard), 
                                     (12,rd.ResourceUsage.Clear)]
                if (res.name == "Offscreen Image"):
                    expectedUsage = [(9,rd.ResourceUsage.Barrier), 
                                     (9,rd.ResourceUsage.Discard), 
                                     (10,rd.ResourceUsage.Clear), 
                                     (42,rd.ResourceUsage.PS_Resource), 
                                     (45,rd.ResourceUsage.PS_Resource), 
                                     (104,rd.ResourceUsage.PS_Resource), 
                                     (105,rd.ResourceUsage.PS_Resource), 
                                     (106,rd.ResourceUsage.PS_Resource), 
                                     (107,rd.ResourceUsage.PS_Resource), 
                                     (112,rd.ResourceUsage.PS_Resource), 
                                     (113,rd.ResourceUsage.PS_Resource), 
                                     (114,rd.ResourceUsage.PS_Resource), 
                                     (145,rd.ResourceUsage.PS_Resource), 
                                     (146,rd.ResourceUsage.PS_Resource), 
                                     (147,rd.ResourceUsage.PS_Resource), 
                                     (148,rd.ResourceUsage.PS_Resource), 
                                     (153,rd.ResourceUsage.PS_Resource), 
                                     (154,rd.ResourceUsage.PS_Resource), 
                                     (155,rd.ResourceUsage.PS_Resource)]
                    if descBuffer:
                        expectedUsage += [
                                     (170+countNested,rd.ResourceUsage.PS_Resource), 
                                     (173+countNested,rd.ResourceUsage.PS_Resource)]
            elif res.type == rd.ResourceType.CommandBuffer:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.DescriptorStore:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            elif res.type == rd.ResourceType.Sampler:
                expectedUsage = [(0,rd.ResourceUsage.Unused)]
            else:
                raise rdtest.TestFailureException(f"'{res.name}' {res.resourceId} Unexpected resource type {res.type.name}")
            rdtest.log.print(f"Resource '{res.name}' type:{res.type.name} {res.resourceId} usages:{len(self.controller.GetUsage(res.resourceId))} expectedUsages:{len(expectedUsage)}")
            self.check_resource_usage(res, expectedUsage)

